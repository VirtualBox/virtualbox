#!/bin/sh

SOCKDIR=$1
WORKDIR=$SOCKDIR/gss

mkdir "$WORKDIR"/k  "$WORKDIR"/d

cat<<EOF > "$WORKDIR"/k/kdc.conf
[realms]
    LIBSSH.SITE = {
        database_name = $WORKDIR/principal
        key_stash_file = $WORKDIR/stash
        kdc_listen = $(hostname -f)
        kdc_tcp_listen = $(hostname -f)
        default_principal_flags = +preauth,+forwardable
    }
[logging]
   kdc = FILE:$WORKDIR/kdc.log
   debug = true
EOF

cat<<EOF > "$WORKDIR"/k/krb5.conf
[libdefaults]
        default_realm = LIBSSH.SITE
        forwardable = true

[realms]
        LIBSSH.SITE = {
                kdc = $(hostname -f)
        }
[domain_realm]
        .$(hostname -d) = LIBSSH.SITE

EOF

kdb5_util -P foo create -s

bash "$WORKDIR"/kadmin.sh

krb5kdc -w 1 -P "$WORKDIR"/pid

# Wait till KDC binds to the ports, 0x58 is port 88
i=0
while [ ! -S "$SOCKDIR"/T0B0058 ] && [ ! -S "$SOCKDIR"/U0B0058 ]; do
    i=$((i + 1))
    [ "$i" -eq 5 ] && exit 1
    sleep 1
done

bash "$WORKDIR"/kinit.sh

klist
exit 0
