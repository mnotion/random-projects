# Argon Applications (prototype)

Paper plugin for the Argon Minecraft application flow.

## Current prototype
- First join sends non-approved players to `application_lobby` in Adventure mode.
- Collects a self-reported first name in chat and asks for confirmation.
- Pending players can parkour; block breaking/placing, combat, fall damage, hunger, item drops/pickups and most commands are blocked.
- Staff commands: `/app list`, `/app approve <player>`, `/app reject <player>`.
- Approval restores the player's saved inventory and sends them to `jawaaa` spawn.
- Floodgate Bedrock detection is reflective (no compile-time Floodgate dependency).

## Build
Requires Java 21 and Maven:
`mvn package`
Jar: `target/argon-applications-0.1.0.jar`

## Install
Upload the jar to GGServers `plugins/`, restart, and ensure worlds named `application_lobby` and `jawaaa` exist.

## Caveats
This is an early prototype. It records first name and pending status locally in `plugins/ArgonApplications/applications.yml`; it does **not** yet connect to ArgonWeb, email applicants, or automatically modify the vanilla whitelist. First names are self-reported, not verified. Test on a backup/staging server before live use.
