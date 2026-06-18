#!/usr/bin/env python3
"""
GGUF to XAI OS Model Converter

Converts GGUF quantized models (INT4/INT6/INT8) to XAI OS native format.
Supports Qwen3, Llama, Mistral, and other modern LLM architectures.

Usage:
    python3 convert_gguf_to_xaios.py \
        --input qwen3-27b-q6_k.gguf \
        --output qwen3-27b-xaios.bin \
        --quantization int6 \
        --context-length 4096

Requirements:
    pip install gguf numpy
"""

import argparse
import struct
import sys
import hashlib
from pathlib import Path

try:
    import gguf
    import numpy as np
except ImportError:
    print("Error: Required packages not installed.")
    print("Install with: pip install gguf numpy")
    sys.exit(1)


# XAI OS Constants
XAIOS_MAGIC = 0x4941494D  # "MIAI"
XAIOS_VERSION = 1
XAIOS_HEADER_SIZE = 80
XAIOS_FLAG_CPU_ONLY = 0x1
XAIOS_TOKENIZER_BPE = 2  # BPE tokenizer (new)
XAIOS_RUNTIME_DETERMINISTIC = 1

# Quantization mappings
QUANT_MAP = {
    'fp32': 0,
    'fp16': 1,
    'int8': 2,
    'int4': 3,
    'q8_8': 4,
    'int6': 5,
}


def fnv1a64_hash(data: bytes) -> int:
    """Compute FNV-1a 64-bit hash (matches XAI OS kernel implementation)."""
    FNV1A64_OFFSET = 14695981039346656037
    FNV1A64_PRIME = 1099511628211
    
    h = FNV1A64_OFFSET
    for byte in data:
        h ^= byte
        h = (h * FNV1A64_PRIME) & 0xFFFFFFFFFFFFFFFF
    return h


def quantize_to_int6(fp32_data: np.ndarray) -> tuple:
    """
    Quantize FP32 weights to INT6 (6-bit signed, range -32 to +31).
    
    Returns:
        tuple: (int6_packed_bytes, scale_factor)
    """
    # Find max absolute value
    max_val = np.max(np.abs(fp32_data))
    
    if max_val == 0:
        scale = 1.0
        packed_size = (fp32_data.size * 3 + 3) // 4
        return b'\x00' * packed_size, scale
    
    # Compute scale (INT6 range: -32 to +31)
    scale = max_val / 31.0
    inv_scale = 1.0 / scale
    
    # Quantize to INT6 range
    quantized = np.clip(np.round(fp32_data * inv_scale), -32, 31).astype(np.int32)
    
    # Pack 4 INT6 values per 3 bytes
    packed = bytearray()
    for i in range(0, len(quantized), 4):
        chunk = quantized[i:i+4]
        # Pad with zeros if less than 4 values
        while len(chunk) < 4:
            chunk = np.append(chunk, 0)
        
        # Pack 4× 6-bit values into 3 bytes
        vals = [int(v) & 0x3F for v in chunk]
        packed_val = vals[0] | (vals[1] << 6) | (vals[2] << 12) | (vals[3] << 18)
        
        packed.append(packed_val & 0xFF)
        packed.append((packed_val >> 8) & 0xFF)
        packed.append((packed_val >> 16) & 0xFF)
    
    return bytes(packed), scale


def quantize_to_int8(fp32_data: np.ndarray) -> tuple:
    """Quantize FP32 weights to INT8 (8-bit signed, range -127 to +127)."""
    max_val = np.max(np.abs(fp32_data))
    
    if max_val == 0:
        return fp32_data.astype(np.int8).tobytes(), 1.0
    
    scale = max_val / 127.0
    inv_scale = 1.0 / scale
    
    quantized = np.clip(np.round(fp32_data * inv_scale), -127, 127).astype(np.int8)
    return quantized.tobytes(), scale


def extract_bpe_tokenizer(reader) -> dict:
    """Extract BPE tokenizer from GGUF file."""
    tokenizer = {
        'type': 'bpe',
        'vocab_size': 0,
        'tokens': [],
        'scores': [],
        'merges': [],
    }
    
    # Extract vocabulary
    if hasattr(reader, 'tokenizer_list'):
        tokenizer['tokens'] = reader.tokenizer_list()
        tokenizer['vocab_size'] = len(tokenizer['tokens'])
    
    # Extract merges (if available)
    if hasattr(reader, 'tokenizer_merge'):
        tokenizer['merges'] = reader.tokenizer_merge()
    
    return tokenizer


def serialize_tokenizer(tokenizer: dict) -> bytes:
    """Serialize BPE tokenizer to binary format."""
    data = bytearray()
    
    # Header
    data.extend(struct.pack('<I', tokenizer['type'] == 'bpe' and 2 or 1))
    data.extend(struct.pack('<I', tokenizer['vocab_size']))
    
    # Tokens
    for token in tokenizer['tokens']:
        token_bytes = token.encode('utf-8') if isinstance(token, str) else token
        data.extend(struct.pack('<I', len(token_bytes)))
        data.extend(token_bytes)
    
    # Merges
    data.extend(struct.pack('<I', len(tokenizer['merges'])))
    for merge in tokenizer['merges']:
        merge_bytes = merge.encode('utf-8') if isinstance(merge, str) else merge
        data.extend(struct.pack('<I', len(merge_bytes)))
        data.extend(merge_bytes)
    
    return bytes(data)


def convert_gguf_to_xaios(input_path: str, output_path: str, quant_type: str,
                          context_length: int):
    """Main conversion function."""
    print(f"Loading GGUF file: {input_path}")
    reader = gguf.GGUFReader(input_path)
    
    # Extract metadata
    arch = reader.architecture
    tensor_count = len(reader.tensors)
    
    # Calculate parameter count
    param_count = 0
    for tensor in reader.tensors:
        param_count += np.prod(tensor.shape)
    
    # Extract architecture-specific metadata
    num_layers = 0
    hidden_size = 0
    num_attention_heads = 0
    
    # Count layers from tensor names (blk.0, blk.1, etc.)
    layer_nums = set()
    for tensor in reader.tensors:
        if 'blk.' in tensor.name:
            # Extract layer number from 'blk.N.'
            parts = tensor.name.split('.')
            for idx, part in enumerate(parts):
                if part == 'blk' and idx + 1 < len(parts):
                    try:
                        layer_num = int(parts[idx + 1])
                        layer_nums.add(layer_num)
                    except ValueError:
                        pass
    num_layers = max(layer_nums) + 1 if layer_nums else 0
    
    # Extract hidden size from attention weights
    for tensor in reader.tensors:
        if 'attn_q.weight' in tensor.name or 'attn_k.weight' in tensor.name:
            if len(tensor.shape) > 0:
                hidden_size = tensor.shape[0]
                break
    
    # Estimate attention heads (hidden_size / 128 is typical for most models)
    if hidden_size > 0:
        num_attention_heads = hidden_size // 128
    
    # Detect model type from architecture name
    model_type = 0  # unknown
    if 'qwen3.5' in arch.lower() or 'qwen3_5' in arch.lower():
        model_type = 1
    elif 'qwen3' in arch.lower() or 'qwen3.' in arch.lower():
        model_type = 2
    
    print(f"Architecture: {arch}")
    print(f"Model type: {model_type} (1=Qwen3.5, 2=Qwen3.6, 0=unknown)")
    print(f"Parameter count: {param_count:,}")
    print(f"Layers: {num_layers}, Hidden: {hidden_size}, Heads: {num_attention_heads}")
    print(f"Target quantization: {quant_type}")
    
    # Extract and quantize weights
    weights_data = bytearray()
    weight_offsets = {}
    scales = {}
    
    quant_func = quantize_to_int6 if quant_type == 'int6' else quantize_to_int8
    
    print("Converting weights...")
    for i, tensor in enumerate(reader.tensors):
        if i % 50 == 0:
            print(f"  Processing tensor {i}/{tensor_count}...")
        
        # Get tensor data as FP32
        tensor_data = tensor.data.astype(np.float32)
        
        # Quantize
        quant_bytes, scale = quant_func(tensor_data)
        
        weight_offsets[tensor.name] = len(weights_data)
        scales[tensor.name] = scale
        weights_data.extend(quant_bytes)
    
    weights_size = len(weights_data)
    print(f"Weights size: {weights_size:,} bytes ({weights_size / 1024**3:.2f} GB)")
    
    # Extract tokenizer
    print("Extracting tokenizer...")
    tokenizer = extract_bpe_tokenizer(reader)
    tokenizer_bytes = serialize_tokenizer(tokenizer)
    tokenizer_offset = XAIOS_HEADER_SIZE + weights_size
    tokenizer_size = len(tokenizer_bytes)
    
    print(f"Tokenizer size: {tokenizer_size:,} bytes")
    print(f"Tokenizer type: BPE ({tokenizer['vocab_size']} tokens)")
    
    # Calculate KV cache requirement from metadata
    if num_layers > 0 and hidden_size > 0:
        kv_bytes_required = 2 * num_layers * hidden_size * context_length * 4
    else:
        # Fallback: estimate from parameter count (rough approximation)
        kv_bytes_required = param_count * 4 // 10
    
    print(f"KV cache required: {kv_bytes_required:,} bytes ({kv_bytes_required / 1024**3:.2f} GB)")
    
    # Build manifest (160 bytes with model metadata)
    manifest_data = bytearray()
    manifest_data.extend(struct.pack('<I', XAIOS_MAGIC))
    manifest_data.extend(struct.pack('<H', XAIOS_VERSION))
    manifest_data.extend(struct.pack('<H', 160))  # 160-byte header with metadata
    manifest_data.extend(struct.pack('<H', QUANT_MAP[quant_type]))
    manifest_data.extend(struct.pack('<H', 0))  # reserved
    manifest_data.extend(struct.pack('<I', XAIOS_FLAG_CPU_ONLY))
    manifest_data.extend(struct.pack('<I', XAIOS_TOKENIZER_BPE))  # BPE tokenizer
    manifest_data.extend(struct.pack('<I', XAIOS_RUNTIME_DETERMINISTIC))
    manifest_data.extend(struct.pack('<Q', XAIOS_HEADER_SIZE))  # weights_offset
    manifest_data.extend(struct.pack('<Q', weights_size))
    manifest_data.extend(struct.pack('<Q', tokenizer_offset))
    manifest_data.extend(struct.pack('<Q', tokenizer_size))
    manifest_data.extend(struct.pack('<Q', kv_bytes_required))
    manifest_data.extend(struct.pack('<Q', 0))  # payload_hash (zero for now)
    manifest_data.extend(struct.pack('<B', 0xAB))  # key
    manifest_data.extend(struct.pack('<B', 16))    # stride
    manifest_data.extend(b'\x00' * 6)              # reserved
    
    # Model metadata (80 bytes)
    manifest_data.extend(struct.pack('<I', model_type))
    manifest_data.extend(struct.pack('<I', param_count))
    manifest_data.extend(struct.pack('<I', num_layers))
    manifest_data.extend(struct.pack('<I', hidden_size))
    manifest_data.extend(struct.pack('<I', num_attention_heads))
    manifest_data.extend(struct.pack('<I', context_length))
    manifest_data.extend(b'\x00' * 56)              # reserved for future fields
    
    # Pad to 160 bytes
    while len(manifest_data) < 160:
        manifest_data.append(0)
    
    # Compute payload hash (weights + tokenizer)
    payload_data = bytes(weights_data) + tokenizer_bytes
    payload_hash = fnv1a64_hash(payload_data)
    
    # Update manifest with correct hash
    manifest_data[56:64] = struct.pack('<Q', payload_hash)
    
    # Write output file
    print(f"Writing XAI OS model: {output_path}")
    with open(output_path, 'wb') as f:
        f.write(bytes(manifest_data))
        f.write(bytes(weights_data))
        f.write(tokenizer_bytes)
    
    total_size = Path(output_path).stat().st_size
    print(f"\nConversion complete!")
    print(f"Output file: {output_path}")
    print(f"Total size: {total_size:,} bytes ({total_size / 1024**3:.2f} GB)")
    print(f"Quantization: {quant_type.upper()}")
    print(f"Context length: {context_length:,} tokens")
    print(f"Payload hash: 0x{payload_hash:016X}")
    
    # Print usage instructions
    print(f"\nUsage in XAI OS:")
    print(f"  1. Copy {output_path} to XAI OS filesystem")
    print(f"  2. Load with: cpu_ai_runtime_load_model_file(arena_id, \"model_name\", \"/path/{Path(output_path).name}\")")
    print(f"  3. Bind to cell: cpu_ai_runtime_bind_model_with_kv(cell_id, arena_id, kv_base, kv_bytes)")
    print(f"  4. Run inference: cpu_ai_runtime_run_model(cell_id, XAIOS_ML_MODEL_DECODE, ...)")


def main():
    parser = argparse.ArgumentParser(
        description='Convert GGUF models to XAI OS native format',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Convert Qwen3.5-0.8B (fast testing, ~3GB)
  python3 convert_gguf_to_xaios.py \\
      --input Qwen3.5-0.8B-Q6_K.gguf \\
      --output qwen3.5-0.8b-xaios.bin \\
      --quantization int6 \\
      --context-length 4096

  # Convert Qwen3.6-27B (production, ~20GB)
  python3 convert_gguf_to_xaios.py \\
      --input Qwen3-27B-Q6_K.gguf \\
      --output qwen3-27b-xaios.bin \\
      --quantization int6 \\
      --context-length 4096

  # Convert Llama 70B with INT8 quantization
  python3 convert_gguf_to_xaios.py \\
      --input llama-70b-q8_0.gguf \\
      --output llama-70b-xaios.bin \\
      --quantization int8 \\
      --context-length 8192
        """
    )
    
    parser.add_argument('--input', required=True,
                       help='Input GGUF file path')
    parser.add_argument('--output', required=True,
                       help='Output XAI OS file path')
    parser.add_argument('--quantization', required=True,
                       choices=['fp32', 'fp16', 'int8', 'int6', 'int4', 'q8_8'],
                       help='Target quantization type')
    parser.add_argument('--context-length', type=int, default=4096,
                       help='Maximum context length (default: 4096)')
    
    args = parser.parse_args()
    
    # Validate input file
    input_path = Path(args.input)
    if not input_path.exists():
        print(f"Error: Input file not found: {input_path}")
        sys.exit(1)
    
    # Validate quantization
    if args.quantization not in QUANT_MAP:
        print(f"Error: Unsupported quantization: {args.quantization}")
        print(f"Supported: {', '.join(QUANT_MAP.keys())}")
        sys.exit(1)
    
    try:
        convert_gguf_to_xaios(
            str(input_path),
            args.output,
            args.quantization,
            args.context_length
        )
    except Exception as e:
        print(f"Error during conversion: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == '__main__':
    main()
