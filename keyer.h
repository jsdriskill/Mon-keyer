#ifndef KEYER_H
#define KEYER_H

#include "config.h"

void keyer_init(void);
void keyer_tick(void);
void keyer_set_sidetone(bool active);
void keyer_update_sidetone_freq(void);
bool keyer_is_busy(void);

/* Text sending for the WinKeyer port. Core 1 calls these; they only touch
 * cross-core queues. */
bool keyer_tx_put(uint8_t c);           /* queue a character; false if the queue is full */
void keyer_tx_clear(void);              /* drop everything queued and stop sending */
uint8_t keyer_tx_pending(void);         /* characters not yet sent */
bool keyer_tx_busy(void);               /* sending or something is queued */
bool keyer_breakin(void);               /* a paddle cancelled a send (cleared by the next put/clear) */
int keyer_echo_get(void);               /* next character to echo to the host, or -1 */
void keyer_set_echo(bool serial, bool paddle);

#endif
