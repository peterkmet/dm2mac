# LLM runs

An LLM run is a playthrough of the game by an LLM player in the turn-based mode ([llm-mode.md](llm-mode.md)). The player's playbook is [llm-playbook.md](llm-playbook.md).

## Goal of the runs

The goal of a run is to complete the game. The runs also measure how much an LLM player improves: from session to session, and against earlier runs.

- Allowed changes to the player instructions between sessions and runs: general rules about how to play and about the game, for example survival, fights, food and water, training, saves, the use of the commands, and how to keep the memory. Change them in small steps and write each change, its reason, and the session where it starts in `coordinator/instruction_changes.md`.
- Not allowed: hints for this dungeon, for example a position, a map, a puzzle solution, where an item lies, or the way to the next level. The memory of an earlier run (maps, lessons) is the player's own knowledge, not a hint.

## Runs and checkpoints

The call log and the commands expect this folder layout. `playthroughs/template/` holds the templates of the player files.

```text
playthroughs/
  current -> run<N>             a link to the current run's folder
  run<N>/
    player/                     the player's memory
    coordinator/                the coordinator files; calls.log of dm2-play
    game/                       the live save folder: QUICK.DAT (--saves)
    saves/<name>/               a checkpoint: a copy of QUICK.DAT, and of player/ in files/
```

- To start a new run:
  1. Create `playthroughs/run<N>/` with `player/`, `coordinator/`, `game/`, and `saves/`.
  2. Copy the files from `playthroughs/template/` to `player/`.
  3. Point the link: `ln -sfn run<N> playthroughs/current`.
  4. Start the game with `--saves playthroughs/current/game` ([llm-mode.md](llm-mode.md#turn-based-mode)).
  5. Send `new_game`, then `save`. The player recruits the champions.
- To make a checkpoint after a session: copy `game/QUICK.DAT` to `saves/<name>/` and the files from `player/` to `saves/<name>/files/`.
- To resume: copy the checkpoint's `QUICK.DAT` to `game/` and its `files/` to `player/`, then send `load`.
- The coordinator's calls stay out of the log: `DM2_PLAY_LOG_DIR=none uv run dm2-play ...`.

## Agents

The two roles are Claude Code agent definitions in the repository:

| Agent | File | Role |
|---|---|---|
| `dm2-coordinator` | [dm2-coordinator.md](../../.claude/agents/dm2-coordinator.md) | Runs a batch of sessions, makes the checkpoints, and keeps the records. |
| `dm2-player` | [dm2-player.md](../../.claude/agents/dm2-player.md) | Plays one session through `dm2-play` and keeps its memory in `playthroughs/current/player/`. |

The main Claude Code session starts the coordinator as a background subagent with a clean context. The coordinator starts one player subagent per session with the goal and a call limit.
