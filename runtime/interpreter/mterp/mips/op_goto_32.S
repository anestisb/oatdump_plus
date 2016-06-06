    /*
     * Unconditional branch, 32-bit offset.
     *
     * The branch distance is a signed code-unit offset, which we need to
     * double to get a byte offset.
     *
     * Unlike most opcodes, this one is allowed to branch to itself, so
     * our "backward branch" test must be "<=0" instead of "<0".
     */
    /* goto/32 +AAAAAAAA */
    FETCH(a0, 1)                           #  a0 <- aaaa (lo)
    FETCH(a1, 2)                           #  a1 <- AAAA (hi)
    sll       a1, a1, 16
    or        rINST, a0, a1                #  rINST <- AAAAaaaa
    b         MterpCommonTakenBranchNoFlags
