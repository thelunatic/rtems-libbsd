/* generated by userspace-header-gen.py */
#include <rtems/linkersets.h>
#include "rtems-bsd-racoon-data.h"
/* algorithm.c */
RTEMS_LINKER_RWSET_CONTENT(bsd_prog_racoon, static struct hash_algorithm oakley_hashdef[]);
RTEMS_LINKER_RWSET_CONTENT(bsd_prog_racoon, static struct hmac_algorithm oakley_hmacdef[]);
RTEMS_LINKER_RWSET_CONTENT(bsd_prog_racoon, static struct enc_algorithm oakley_encdef[]);
RTEMS_LINKER_RWSET_CONTENT(bsd_prog_racoon, static struct enc_algorithm ipsec_encdef[]);
RTEMS_LINKER_RWSET_CONTENT(bsd_prog_racoon, static struct hmac_algorithm ipsec_hmacdef[]);
RTEMS_LINKER_RWSET_CONTENT(bsd_prog_racoon, static struct misc_algorithm ipsec_compdef[]);
RTEMS_LINKER_RWSET_CONTENT(bsd_prog_racoon, static struct misc_algorithm oakley_authdef[]);
RTEMS_LINKER_RWSET_CONTENT(bsd_prog_racoon, static struct dh_algorithm oakley_dhdef[]);