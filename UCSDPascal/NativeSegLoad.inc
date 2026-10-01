// NativeSegLoad.inc -- shared native GETSEG disk-read path (GSGREAD + READSEG
// + RLSEG/RLLIST), included by NativeCxp.inc (CXP) and NativeCsp.inc (GSEG).
// Needs the host macros plus a host member
//   bool NativeDiskRead(uint16_t unit, uint16_t block, uint16_t len, std::vector<uint8_t>& out)
// that returns the raw bytes of `len` bytes starting at block*512 of `unit`,
// or false if that unit isn't handled natively (then nothing is changed and
// the caller falls through to the real Z80 code).
//
// Two phases, so nothing is written unless the whole load will succeed:
//   slProbe(seg, spP, plan)  read-only: SEGTBL lookup, disk read into a
//                            buffer, (NEWSEG)^ = seg check. spP = the P-code
//                            SP at the moment GETSEG is CALLed.
//   slCommit(plan)           performs READSEG's and GSGREAD's writes exactly:
//                            TPROC vars, the segment bytes (onto the stack,
//                            SP := SEGBOT), IORSLT := 0, relocation, then
//                            refcount := 1 and INTSEGT[seg].addr := NEWSEG.
// The real code's 8-bit "DEC L"/"INC L" address steps are reproduced via
// slDecL/slIncL. SYSIO/GETU's parameter-space variables are written too.
// Not reproduced: the disk driver's own internals and the return addresses
// / parameters SYSRD leaves in memory below SEGBOT (junk in the not-yet-
// initialised local data area of the procedure about to be called).
struct SegLoadPlan {
    uint8_t  seg = 0;
    uint16_t segtp = 0, len = 0, unit = 0, block = 0, newseg = 0, segbot = 0, refLo = 0;
    std::vector<uint8_t> image;
    bool inImage(uint16_t a, uint16_t n) const {
        uint16_t off = (uint16_t)(a - segbot);
        return (uint32_t)off + n <= len;
    }
    uint16_t img16(uint16_t a) const {
        uint16_t off = (uint16_t)(a - segbot);
        return (uint16_t)(image[off] | (image[(uint16_t)(off + 1)] << 8));
    }
};
auto slRd   = [&](uint16_t a) -> uint16_t { return (uint16_t)(PM_MEM[a] | (PM_MEM[(uint16_t)(a + 1)] << 8)); };
auto slWr   = [&](uint16_t a, uint16_t v) { PM_MEM[a] = (uint8_t)(v & 0xFF); PM_MEM[(uint16_t)(a + 1)] = (uint8_t)(v >> 8); };
auto slCRd  = [&](uint16_t a) -> uint16_t { return PM_CODE16(a); };      // code: the segment being loaded
auto slCWr  = [&](uint16_t a, uint16_t v) { PM_CODEW8(a, v & 0xFF); PM_CODEW8((uint16_t)(a + 1), v >> 8); };
auto slDecL = [](uint16_t a) -> uint16_t { return (uint16_t)((a & 0xFF00) | ((a - 1) & 0x00FF)); };
auto slIncL = [](uint16_t a) -> uint16_t { return (uint16_t)((a & 0xFF00) | ((a + 1) & 0x00FF)); };

auto slProbe = [&](uint8_t seg, uint16_t spP, SegLoadPlan& p) -> bool {
    p.seg   = seg;
    p.segtp = (uint16_t)(PM_V(0x0348) + 6 * (uint16_t)seg);   // SEGTBL+4 + 6*seg  (^seg_len)
    p.len   = slRd(p.segtp);
    if (p.len == 0) return false;                       // NOPROC
    p.unit  = slRd((uint16_t)(p.segtp - 4));
    p.block = slRd((uint16_t)(p.segtp - 2));
    p.newseg = (uint16_t)(spP - 2);                     // READSEG: HL := SP (its own ret addr is pushed)
    uint16_t h = (uint16_t)(p.newseg - p.len);          // SUBHLDE
    h = slIncL(h); h = (uint16_t)(h + 1);               // INC L; INC HL
    p.segbot = h;
    if (!p.inImage(p.newseg, 2)) return false;
    if (!NativeDiskRead(p.unit, p.block, p.len, p.image) || p.image.size() != p.len) return false;
    if (p.image[(uint16_t)(p.newseg - p.segbot)] != seg) return false; // NOPROC
    p.refLo = slDecL((uint16_t)(PM_V(0x0251) + 4 * (uint16_t)seg));          // GETSEG's HL at "JP Z,GSGREAD"
    return true;
};

// ---- Errors while loading a code segment (register compatibility off) ----
// slLoad classifies what READSEG would do; slError reproduces the exact
// state at READSEG's error exits and returns the error label to continue at
// (NativeErrors.inc):
//   no such segment (seg_len 0)   -> NOPROC  (13E8)
//   the disk read failed          -> SYIOER  (1420)
//   the block read holds another segment -> NOPROC (142A)
// Anything unusual (a unit that is not a Big Disk unit, a segment too short
// to check) is SL_DECLINE and stays with the Z80 code, as before.
enum { SL_OK = 0, SL_NOSEG, SL_IOERR, SL_WRONGSEG, SL_DECLINE };
auto slDiskUnit = [&](uint16_t unit) -> bool {       // GETU accepts it for input, and its driver is the Big Disk one
    const PmIoConfig& io = PM_IOCFG;
    const uint8_t u = (uint8_t)unit;
    return io.ok && u != 0 && u < io.maxu1 && (io.flags[u] & 0x01) && io.kind[u] == PMIO_DISK;
};
uint16_t slSpP = 0;                                  // the caller's SP (READSEG's return address goes just below)
auto slLoad = [&](uint8_t seg, uint16_t spP, SegLoadPlan& p) -> int {
    slSpP = spP;
    p.seg   = seg;
    p.segtp = (uint16_t)(PM_V(0x0348) + 6 * (uint16_t)seg);
    p.refLo = slDecL((uint16_t)(PM_V(0x0251) + 4 * (uint16_t)seg));
    p.len   = slRd(p.segtp);
    if (p.len == 0) return SL_NOSEG;
    p.unit  = slRd((uint16_t)(p.segtp - 4));
    p.block = slRd((uint16_t)(p.segtp - 2));
    p.newseg = (uint16_t)(spP - 2);
    uint16_t h = (uint16_t)(p.newseg - p.len);
    h = slIncL(h); h = (uint16_t)(h + 1);
    p.segbot = h;
    if (!p.inImage(p.newseg, 2)) return SL_DECLINE;
    if (NativeDiskRead(p.unit, p.block, p.len, p.image) && p.image.size() == p.len)
        return p.image[(uint16_t)(p.newseg - p.segbot)] == seg ? SL_OK : SL_WRONGSEG;
    return slDiskUnit(p.unit) ? SL_IOERR : SL_DECLINE;
};
auto slError = [&](SegLoadPlan& p, int kind, uint16_t getsegRet) -> uint16_t {
    slWr(PM_V(0x0290), getsegRet);           // GETSEG: RETADR2 := its return address
    PM_MEM[PM_V(0x02C6)] = p.seg;            // SEGNUM
    slWr(PM_V(0x02C0), p.refLo);             // GSGREAD: REFP
    slWr(PM_V(0x02C8), p.segtp);             // READSEG: SEGTP
    if (kind == SL_NOSEG) {            // JP Z,NOPROC -- only READSEG's return address pushed
        PM_CPU.r.SP = (uint16_t)(slSpP - 2);
        slWr(PM_CPU.r.SP, 0x14C0);
        return 0x03DF;
    }
    slWr(PM_V(0x02CA), p.newseg);            // NEWSEG
    slWr(PM_V(0x02BC), p.segbot);            // SEGBOT; stack extended by seg_len, return address re-pushed
    PM_CPU.r.SP = (uint16_t)(p.segbot - 2);
    slWr(PM_CPU.r.SP, 0x14C0);
    PM_MEM[PM_V(0x02D2)] = 0x01;             // SYSRD / SYSIO: UREQ, URTN, UASY, UBLK, ULEN, UBUF
    slWr(PM_V(0x02DC), 0x141C); slWr(PM_V(0x02DA), 0); slWr(PM_V(0x02D8), p.block); slWr(PM_V(0x02D6), p.len); slWr(PM_V(0x02D4), p.segbot);
    PM_MEM[PM_V(0x02E4)] = 0;                // GETU: IORSLT := 0 (low byte), UNIT, UPTR
    PM_MEM[PM_V(0x02D3)] = (uint8_t)p.unit;
    slWr(PM_V(0x02D0), (uint16_t)(0x1AFB + 4 * (uint8_t)p.unit));
    if (kind == SL_IOERR) {            // the Big Disk driver, through the engine's own ports
        PortOut(0xC8, (uint8_t)p.unit);
        PortOut(0xCD, (uint8_t)(p.block >> 8)); PortOut(0xCE, (uint8_t)p.block);
        PortOut(0xCB, (uint8_t)(p.len >> 8));   PortOut(0xCC, (uint8_t)p.len);
        PortOut(0x10, (uint8_t)(p.segbot >> 8)); PortOut(0x0F, (uint8_t)p.segbot);
        PortOut(0xCF, 0x01);
        PM_MEM[PM_V(0x02E4)] = PortIn(0xCF);                // status -> IORSLT
        return 0x0408;                                // JP NZ,SYIOER
    }
    for (uint16_t i = 0; i < p.len; i++) PM_CODEW8((uint16_t)(p.segbot + i), p.image[i]);   // read in
    return 0x03DF;                                    // wrong segment: JP NZ,NOPROC
};

auto slRlList = [&](uint16_t de, uint16_t delta) -> uint16_t {   // RLLIST; returns HL = ^last node
    slWr(PM_V(0x02C4), delta);                                            // RLDELTA
    uint16_t h = (uint16_t)(de - 1);
    uint8_t b = PM_CODE8(h); h = slDecL(h); uint8_t c = PM_CODE8(h);
    uint16_t n = (uint16_t)((b << 8) | c);
    while (n != 0) {
        h = (uint16_t)(h - 1);
        uint8_t d = PM_CODE8(h); h = slDecL(h);
        uint16_t tgt = (uint16_t)(h - (uint16_t)((d << 8) | PM_CODE8(h)));
        slCWr(tgt, (uint16_t)(slCRd(tgt) + delta));
        n = (uint16_t)(n - 1);
    }
    return h;
};

auto slCommit = [&](SegLoadPlan& p) {
    PM_MEM[PM_V(0x02C6)] = p.seg;            // SEGNUM (byte, GETSEG "LD (SEGNUM),A")
    slWr(PM_V(0x02C0), p.refLo);             // REFP
    slWr(PM_V(0x02C8), p.segtp);             // SEGTP
    slWr(PM_V(0x02CA), p.newseg);            // NEWSEG
    slWr(PM_V(0x02BC), p.segbot);            // SEGBOT
    for (uint16_t i = 0; i < p.len; i++) PM_CODEW8((uint16_t)(p.segbot + i), p.image[i]);
    slWr(PM_V(0x02E4), 0);                   // IORSLT := 0 (GETU; SYSRD success)
    // SYSRD -> SYSIO / GETU parameter-space writes (later Z80 I/O code can
    // pick up leftovers from these, so they matter for register fidelity):
    PM_MEM[PM_V(0x02D2)] = 0x01;             // UREQ := INBIT
    slWr(PM_V(0x02DC), 0x141C);              // URTN := READSEG's return point after CALL SYSRD
    slWr(PM_V(0x02DA), 0);                   // UASY := 0 (SYSRD's synthesized async param)
    slWr(PM_V(0x02D8), p.block);             // UBLK
    slWr(PM_V(0x02D6), p.len);               // ULEN
    slWr(PM_V(0x02D4), p.segbot);            // UBUF := base + index(0)
    PM_MEM[PM_V(0x02D3)] = (uint8_t)p.unit;          // UNIT (GETU)
    slWr(PM_V(0x02D0), (uint16_t)(0x1AFB + 4 * (uint8_t)p.unit)); // UPTR := ^UNITBL[unit]
    PM_CPU.r.SP = p.segbot;            // stack now extended by the segment

    // GSGREAD: relocation base from the segment's procedure 1
    uint16_t a = (uint16_t)(p.newseg - 1);
    uint8_t d = PM_CODE8(a); a = slDecL(a);
    uint16_t j1 = (uint16_t)(a - (uint16_t)((d << 8) | PM_CODE8(a)));
    uint16_t rlbase;
    if (PM_CODE8(j1) == 0 || PM_CODE8((uint16_t)(j1 + 1)) != 0) {
        rlbase = slRd(PM_V(0x02F0));                               // $05: current BASE
    } else {                                                 // $10: what BASE will be
        uint16_t q = (uint16_t)(j1 + 1 - 6);
        uint8_t pd = PM_CODE8(q); q = slDecL(q); uint8_t pe = PM_CODE8(q);
        q = (uint16_t)(q - 1); uint8_t db = PM_CODE8(q); q = slDecL(q); uint8_t dc = PM_CODE8(q);
        uint16_t sum = (uint16_t)((uint16_t)((pd << 8) | pe) + (uint16_t)((db << 8) | dc) + 12);
        rlbase = (uint16_t)(PM_CPU.r.SP - sum);
    }
    slWr(PM_V(0x02BE), rlbase);              // RLBASE

    // RLSEG: every assembly procedure gets its entric made absolute and its
    // base-, segment- and proc-relative lists relocated
    uint16_t h = slIncL(p.newseg);
    uint8_t procs = PM_CODE8(h);
    h = slDecL(h);
    do {
        h = (uint16_t)(h - 1);
        uint8_t dd = PM_CODE8(h); h = slDecL(h);
        uint16_t jt = (uint16_t)(h - (uint16_t)((dd << 8) | PM_CODE8(h)));
        uint16_t saved = h;
        if (PM_CODE8(jt) == 0) {
            uint16_t e = slDecL((uint16_t)(jt - 1));
            uint16_t e1 = slIncL(e);
            uint16_t absEntry = (uint16_t)(e - (uint16_t)((PM_CODE8(e1) << 8) | PM_CODE8(e)));
            PM_CODEW8(e, absEntry & 0xFF);
            PM_CODEW8(e1, absEntry >> 8);
            slWr(PM_V(0x02C2), absEntry);                           // PROCBOT
            uint16_t last = slRlList(e, rlbase);              // base-relative
            last = slRlList(last, p.segbot);                  // segment-relative
            slRlList(last, absEntry);                         // proc-relative
        }
        h = saved;
        procs = (uint8_t)(procs - 1);
    } while (procs != 0);

    PM_MEM[p.refLo] = (uint8_t)(PM_MEM[p.refLo] + 1);        // INC (HL): refcount := 1
    uint16_t ap = (uint16_t)(slIncL(p.refLo) + 1);
    PM_MEM[ap] = (uint8_t)(p.newseg & 0xFF);
    ap = slIncL(ap);
    PM_MEM[ap] = (uint8_t)(p.newseg >> 8);
};
