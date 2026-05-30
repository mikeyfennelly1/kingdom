PATCHY
1. get working from vm to client laptop does tls n stuff work the same?
2. Fix blockchain hash storage model
     The contract stores only one hash per conversationId, so every new message overwrites the previous hash. Better: key by messageId or
     store an append-only record id. Code: blockchain/contracts/MessageIntegrity.sol:22.
3. Handle blockchain tx mining failures
     The sidecar returns txHash before tx.wait(). If mining later fails, the DB still stores a bad/unconfirmed tx hash. Code: blockchain/
     sidecar/index.js:43.
4. few docs i suppose not crypto ones
MAKE SURE FRONTEND IS WORKING CORRECTLY AND HAS ALL FUNCTIONALITY AND SHI

FIONN
1. password stuff e.g. 12 characters etc, forgot password or no?
2. encrypt local store?
3. [x] one-time prekeys are now consumed only transactionally during message creation; standalone consume endpoint removed.
4. do full signal / ratchet stuff????
5. crypto doc or sum
make sure dependecnys pinned
MAKE SURE FRONTEND IS WORKING CORRECTLY AND HAS ALL FUNCTIONALITY AND SHI
