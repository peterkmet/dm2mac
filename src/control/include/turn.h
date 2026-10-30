/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_CONTROL_TURN_H
#define DM2_CONTROL_TURN_H
/* Prefixes: turn_ */

/* Turns of the turn-based mode: an action gives its inputs to the input queue, the game runs one game step per frame
   until the party can act again; a batch runs a list of actions and a wait runs game steps, both stop on news. */

#include <stdbool.h>
#include "json.h"

/* The state of the turn */

/* A turn or a batch runs: the client waits for its reply. */
bool turn_is_pending(void);
/* The client left: the turn runs on to its end without a reply. */
void turn_drop_reply(void);

/* Requests: each starts a turn, or returns the reason why it cannot (the game does not change then). */

/* An act turn: the inputs of one action. */
const char *turn_act(const char *request);
/* A wait turn: game ticks without inputs, until the ticks ran or news came. */
const char *turn_wait(const char *request);
/* A batch of act turns; a batch that ends before its first turn has its reply ready at once (no turn is pending). */
const char *turn_act_many(const char *request);

/* Frames */

/* One frame of the pending turn: gives the next input when the input is idle, records what the party sees, and checks
   the end. Returns true when the reply is ready (turn_reply writes it). */
bool turn_poll(void);
/* The reply of the finished turn or batch: ticks, stops, game_over, damage, look, seen, state, view. */
void turn_reply(Json *j);

#endif /* DM2_CONTROL_TURN_H */
