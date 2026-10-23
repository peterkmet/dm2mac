# LLM runs

An LLM run is a playthrough of the game by an LLM player in the turn-based mode ([llm-mode.md](llm-mode.md)). The player manual is [llm-playbook.md](llm-playbook.md).

## Goal of the runs

The goal of the runs is to measure how much an LLM player gets better: from session to session, and against earlier runs. To finish the game is the long-term goal, not the measure of one run. The main session of Claude Code starts the coordinator, the agent `dm2-coordinator` ([Agents](#agents)). The coordinator starts the sessions and keeps the records.

- Allowed changes of the player instructions between sessions and runs: general rules about how to play and about the game, for example survival, fights, food and water, training, saves, the use of the commands, and how to keep the memory. Change them in small steps and write each change, its reason, and the session where it starts.
- Not allowed: hints for this dungeon, for example a position, a map, the solution of a puzzle, where an item lies, or the way to the next level. The memory of an earlier run (maps, lessons) is the player's own knowledge, not a hint.

## Runs and checkpoints

The template of the player files is `playthroughs/template/`. The folder layout below is the convention that the call log and the commands expect.

```text
playthroughs/
  current -> run<N>             a link to the folder of the current run
  run<N>/
    player/                     the memory of the player (the player sees only this)
    coordinator/                the coordinator files; calls.log of dm2-play
    game/                       the live save folder: QUICK.DAT (--saves)
    saves/<name>/               a checkpoint: a copy of QUICK.DAT and of player/
```

- A new run: create `playthroughs/run<N>/` with `player/`, `coordinator/`, `game/`, and `saves/`, and copy the files of `playthroughs/template/` to `player/`. Point the link: `ln -sfn run<N> playthroughs/current`. Start the game with `--saves playthroughs/current/game` ([Start the game](llm-mode.md#turn-based-mode)). Send `new_game`, then `save`. The player recruits the champions.
- A checkpoint after a session: copy `game/QUICK.DAT` and the files of `player/` to `saves/<name>/`.
- A resume: copy `QUICK.DAT` of the checkpoint to `game/` and its files to `player/`, then send `load`.
- The calls of the coordinator stay out of the log: `DM2_PLAY_LOG_DIR=none uv run dm2-play ...`.

## Agents

The two roles are agent definitions of Claude Code in the repository:

| Agent | File | Role |
|---|---|---|
| `dm2-coordinator` | [dm2-coordinator.md](../../.claude/agents/dm2-coordinator.md) | Plays a batch of sessions, makes the checkpoints, and keeps the records. |
| `dm2-player` | [dm2-player.md](../../.claude/agents/dm2-player.md) | Plays one session through `dm2-play` and keeps its memory in `playthroughs/current/player/`. |

The coordinator runs as a background subagent of Claude Code with a clean context. It starts one player subagent per session with the goal and a call limit.
