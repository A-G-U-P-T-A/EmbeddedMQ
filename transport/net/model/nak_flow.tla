---- MODULE nak_flow ----
EXTENDS Naturals

CONSTANTS MaxWindow, MaxRetries

VARIABLES sent, acked, nak_count, state

vars == <<sent, acked, nak_count, state>>

Init ==
  /\ sent = 0
  /\ acked = 0
  /\ nak_count = 0
  /\ state = "idle"

SendData ==
  /\ state \in {"idle", "await_ack"}
  /\ sent < MaxWindow
  /\ sent' = sent + 1
  /\ state' = "await_ack"
  /\ UNCHANGED <<acked, nak_count>>

RecvAck ==
  /\ state = "await_ack"
  /\ acked' = acked + 1
  /\ state' = IF acked + 1 = sent THEN "idle" ELSE "await_ack"
  /\ UNCHANGED <<sent, nak_count>>

RecvNak ==
  /\ state = "await_ack"
  /\ nak_count < MaxRetries
  /\ nak_count' = nak_count + 1
  /\ state' = "retransmit"
  /\ UNCHANGED <<sent, acked>>

Retransmit ==
  /\ state = "retransmit"
  /\ state' = "await_ack"
  /\ UNCHANGED <<sent, acked, nak_count>>

Stall ==
  /\ state = "await_ack"
  /\ nak_count = MaxRetries
  /\ UNCHANGED vars

Next ==
  \/ SendData
  \/ RecvAck
  \/ RecvNak
  \/ Retransmit
  \/ Stall

TypeOK ==
  /\ sent \in Nat
  /\ acked \in Nat
  /\ nak_count \in Nat
  /\ acked <= sent
  /\ state \in {"idle", "await_ack", "retransmit"}

Inv ==
  /\ TypeOK
  /\ sent <= MaxWindow

Spec == Init /\ [][Next]_vars

THEOREM Spec => []Inv
