#ifndef TURN_H
#define TURN_H

#include <stdbool.h>
#include "state_json.h"

/* Prefixes: turn_ */

/* Turns of the turn-based mode: an action gives its inputs to the input queue, the game runs one game step per frame
   until the party can act again; a batch runs a list of actions and a wait runs game steps, both stop on news. */
bool turn_is_pending(void);
/* Each starts a turn, or returns the reason why it cannot (the game does not change then). */
const char *turn_act(const char *request);
/* A batch of act turns; a batch that ends before its first turn has its reply ready at once (no turn is pending). */
const char *turn_act_many(const char *request);
const char *turn_wait(const char *request);
/* One frame of the pending turn: gives the next input when the input is idle, records what the party sees, and checks
   the end. Returns true when the reply is ready (turn_reply writes it). */
bool turn_poll(void);
/* The reply of the finished turn or batch: ticks, stops, game_over, damage, look, seen, state, view. */
void turn_reply(Json *j);
/* The client left: the turn runs on to its end without a reply. */
void turn_drop_reply(void);

#endif
