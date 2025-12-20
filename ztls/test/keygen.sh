#!/bin/sh
mbedtls_gen_key type=rsa rsa_keysize=4096 filename=ZtlsServer.key
mbedtls_cert_write selfsign=1 issuer_key=ZtlsServer.key issuer_name='CN=localhost,O=org,C=US' not_before=19700101000000 not_after=20401231235959 is_ca=1 max_pathlen=0 output_file=ZtlsServer.crt
