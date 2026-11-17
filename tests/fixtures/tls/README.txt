Test-only certificates for the TLS client tests (tests/test_net.cpp). The private keys are public on
purpose and protect nothing. Regenerate with: sh make.sh
- ca.pem            test root (trusted only inside the tests)
- server.pem/.key   "localhost", signed by the test root (valid for DNS:localhost only, not 127.0.0.1)
- rogue.pem/.key    self-signed "localhost" that nobody trusts
