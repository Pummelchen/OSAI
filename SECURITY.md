# Security Policy

OSAI is currently in the design and planning stage. It is not a production operating system and should not be treated as deployed infrastructure.

## Reporting

Production vulnerability reporting is not active yet because there is no production release. For now, report security design concerns through repository issues or pull requests.

## Secret Handling

Never commit credentials, GitHub tokens, API keys, private keys, SSH keys, passwords, or benchmark data that contains secrets.

If a secret is exposed, revoke and rotate it immediately before continuing development.

## Security Model

The OSAI security model is evolving. Current design priorities include capability-based resource ownership, app-agent permissions, Git/repository write controls, build/test sandboxes, signed updates, SSH-only administration, patch review, and rollback.
