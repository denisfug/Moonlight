#!/bin/sh
set -e
cd "$(dirname "$0")"
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout ca.key -out ca.pem -days 7300 -subj "/CN=Moonlight Test Root"
openssl req -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout server.key -out server.csr -subj "/CN=localhost"
printf "subjectAltName=DNS:localhost\nbasicConstraints=CA:FALSE\n" > ext.cnf
openssl x509 -req -in server.csr -CA ca.pem -CAkey ca.key -CAcreateserial -out server.pem -days 7000 -extfile ext.cnf
openssl req -x509 -newkey rsa:2048 -nodes -keyout rogue.key -out rogue.pem -days 7000 -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost"
rm -f server.csr ext.cnf ca.srl ca.key
