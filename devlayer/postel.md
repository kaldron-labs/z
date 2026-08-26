`plan.md`: apply Postel's Law:
- negative protocol standards conformance is a non-goal (testing for failures and diagnostics on invalid inputs)
  - invalid input processing and validation:
    - invalid inputs should be handled gracefully
    - invalid inputs should not crash the receiver or present a security risk (buffer overruns, etc.)
      - completely corrupt inputs should result in connection/session/stream force-close with
        no diagnostic send to peer (DoS protection)
    - valid but unsupported inputs should be handled conformantly
- positive protocol interoperability and standards conformance is a goal
  - testing for correct outputs that are consumable by other implementations
  - correctly ingesting inputs from other implementations
- scrutinize all validation requirements and **delete unnecessary validations**
  - explicitly prohibit unnecessary validations so the ensuing implementation does not inadvertently implement them
