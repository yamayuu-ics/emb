#ifndef _SERIAL_H_INCLUDE_
#define _SERIAL_H_INCLUDE_

int serial_init(int index);
int serial_is_send_enable(int index);
int serial_send_byte(int index,unsigned char b);
int serial_is_recv_enable(int index);
unsigned char serial_recv_byte(int index);

int serial_intr_is_send_enable(int index);        /* 送信割込み有効か？ */
void serial_intr_send_enable(int index);          /* 送信割込み有効化 */
void serial_intr_send_disable(int index);         /* 送信割込み無効化 */
int serial_intr_is_recv_enable(int index);        /* 受信割込み有効か？ */
void serial_intr_recv_enable(int index);          /* 受信割込み有効化 */
void serial_intr_recv_disable(int index);         /* 受信割込み無効化 */

#endif

