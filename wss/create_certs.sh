#!/bin/bash

CERTDIR=./certs

KEY=$CERTDIR/server.key
CSR=$CERTDIR/server.csr
CRT=$CERTDIR/server.crt

set -xe

openssl genrsa -out $KEY 4096

openssl req -new -key $KEY -out $CSR

openssl x509 -req -days 365 -in $CSR -signkey $KEY -out $CRT
