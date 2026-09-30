// NativeOps.inc -- shared by PSystemEngine.cpp and the linux-harness copies;
// included inside the main native-opcode switch. Native C versions of the
// last p-code operators that had none:
//   real arithmetic  FLT FLO ABR ADR SBR MPR SQR DVR NGR  (UcsdReal.h,
//                    bit-exact against the Z80 FP package -- see fptest)
//   set / integer    INT (set intersection)  SQI (square integer)
//   string index     IXS
//   jumps            EFJ NFJ
//   debugger         BPT
// Like NativeCsp.inc these do not reproduce the Z80 routines' leftover
// A/DE/HL, so they run only when Preserve Z80 Register Compatibility is OFF.
// Any case that the real routine turns into a run-time error (FP overflow/
// underflow/divide-by-zero, IXS bad index, BPT halt) or that needs the
// (EFJ/NFJ long jump-table jumps are native too since v1.73) is detected first and left entirely
// to the Z80 code, with nothing changed. ABORT and HLT are error exits by
// definition and stay on the Z80 path.
// Host must #define PM_MEM, PM_CPU, PM_PRESERVE, PM_COUNT(op) and include
// UcsdReal.h at file scope.
#define NO_RD16(a)    ((uint16_t)(PM_MEM[(uint16_t)(a)] | (PM_MEM[(uint16_t)((a) + 1)] << 8)))
#define NO_WR16(a, v) do { uint16_t na_ = (uint16_t)(a), nv_ = (uint16_t)(v); \
                           PM_MEM[na_] = (uint8_t)nv_; PM_MEM[(uint16_t)(na_ + 1)] = (uint8_t)(nv_ >> 8); } while (0)
#define NO_PEEKREAL(rv, off) do { for (int ni_ = 0; ni_ < 4; ni_++) (rv)[ni_] = PM_MEM[(uint16_t)(PM_CPU.r.SP + (off) + ni_)]; } while (0)
#define NO_PUSHREAL(rv) do { PushStackWord((uint16_t)((rv)[2] | ((rv)[3] << 8))); \
                            PushStackWord((uint16_t)((rv)[0] | ((rv)[1] << 8))); } while (0)
// FPLBEG (SAVIPC, FPERROR := 0) + FPLSTOR's RETADR; then BACK1 == BACK
#define NO_FPDONE(op, ret) do { NO_WR16(PM_V(0x0246), PM_CPU.r.BC()); PM_MEM[PM_V(0x0248)] = 0; \
                                if (ret) NO_WR16(PM_V(0x024A), (ret)); commit = true; PM_COUNT(op); } while (0)
// The same, when the operation raised a floating point error: the Z80
// package leaves its error result on the stack exactly like a result
// (UcsdReal provides it), FPERROR = 1, and FPLCHK goes to FPIERR.
#define NO_FPERR(op, ret) do { NO_WR16(PM_V(0x0246), PM_CPU.r.BC()); PM_MEM[PM_V(0x0248)] = 1; \
                               if (ret) NO_WR16(PM_V(0x024A), (ret)); PM_CPU.r.PC = 0x041B; PM_COUNT(op); } while (0)

            case OP_ADR:
            case OP_SBR:
            case OP_MPR:
            case OP_DVR: {
                if (PM_PRESERVE) break;
                uint8_t a2[4], a1[4], r[4];
                NO_PEEKREAL(a2, 0); NO_PEEKREAL(a1, 4);
                uint8_t op = (uint8_t)nativeOpcodeLookup;
                bool ok = op == OP_ADR ? ucsdreal::AddSub(a1, a2, false, r)
                        : op == OP_SBR ? ucsdreal::AddSub(a1, a2, true, r)
                        : op == OP_MPR ? ucsdreal::Mul(a1, a2, r) : ucsdreal::Div(a1, a2, r);
                PM_CPU.r.SP = (uint16_t)(PM_CPU.r.SP + 8);
                NO_PUSHREAL(r);                                  // result, or the package's error result
                const uint16_t ret = op == OP_ADR ? 0x10D7 : op == OP_SBR ? 0x10E0 : op == OP_MPR ? 0x10E9 : 0x10FB;
                if (ok) NO_FPDONE(op, ret); else NO_FPERR(op, ret);
                break;
            }
            case OP_SQR: {
                if (PM_PRESERVE) break;
                uint8_t x[4], r[4];
                NO_PEEKREAL(x, 0);
                const bool ok = ucsdreal::Mul(x, x, r);
                PM_CPU.r.SP = (uint16_t)(PM_CPU.r.SP + 4);
                NO_PUSHREAL(r);
                if (ok) NO_FPDONE(OP_SQR, 0x0F95);               // FPFMUL called from FPFSQR
                else NO_FPERR(OP_SQR, 0x0F95);
                break;
            }
            case OP_NGR:
            case OP_ABR: {
                if (PM_PRESERVE) break;
                uint16_t hi = (uint16_t)(PM_CPU.r.SP + 1);       // sign lives in byte 1 of the real
                if (nativeOpcodeLookup == OP_NGR) PM_MEM[hi] ^= 0x80; else PM_MEM[hi] &= 0x7F;
                NO_FPDONE((uint8_t)nativeOpcodeLookup, 0);
                break;
            }
            case OP_FLT: {                                       // float the integer on top
                if (PM_PRESERVE) break;
                uint8_t r[4];
                ucsdreal::Float(PopStackWord(), r);
                NO_PUSHREAL(r);
                NO_FPDONE(OP_FLT, 0x10AC);
                break;
            }
            case OP_FLO: {                                       // float the integer under the real
                if (PM_PRESERVE) break;
                uint16_t w0 = PopStackWord(), w1 = PopStackWord();
                NO_WR16(PM_V(0x0290), w0); NO_WR16(PM_V(0x0292), w1);        // REAL1 / REAL2 (WORD1 / WORD2)
                uint8_t r[4];
                ucsdreal::Float(PopStackWord(), r);
                NO_PUSHREAL(r);
                PushStackWord(w1); PushStackWord(w0);
                NO_FPDONE(OP_FLO, 0x10BD);
                break;
            }
            case OP_INT: {                                       // set intersection (AND), zero-fill the rest of set_a
                if (PM_PRESERVE) break;
                uint16_t sp = PM_CPU.r.SP;
                uint8_t szb = (uint8_t)PeekStackWord(0);
                uint16_t szaAddr = (uint16_t)(sp + 2 * (1 + szb));
                uint8_t sza = PM_MEM[szaAddr];
                NO_WR16(PM_V(0x0246), PM_CPU.r.BC());                  // SETUP's SAVIPC
                uint8_t mn = sza < szb ? sza : szb, fill = sza > szb ? (uint8_t)(sza - szb) : 0;
                uint16_t b = (uint16_t)(sp + 2), a = (uint16_t)(szaAddr + 2);
                for (uint8_t i = 0; i < mn; i++, a = (uint16_t)(a + 2), b = (uint16_t)(b + 2)) {
                    PM_MEM[a] &= PM_MEM[b];
                    PM_MEM[(uint16_t)(a + 1)] &= PM_MEM[(uint16_t)(b + 1)];
                }
                for (uint8_t i = 0; i < fill; i++, a = (uint16_t)(a + 2)) { PM_MEM[a] = 0; PM_MEM[(uint16_t)(a + 1)] = 0; }
                PM_CPU.r.SP = szaAddr;                           // SP := NEWSP
                commit = true; PM_COUNT(OP_INT);
                break;
            }
            case OP_SQI: {                                       // square integer (MULT, low 16 bits)
                if (PM_PRESERVE) break;
                NO_WR16(PM_V(0x0246), PM_CPU.r.BC());
                uint16_t v = PopStackWord();
                PushStackWord((uint16_t)(v * v));
                commit = true; PM_COUNT(OP_SQI);
                break;
            }
            case OP_IXS: {                                       // index string check: 1 <= index <= length
                if (PM_PRESERVE) break;
                uint16_t idx = PeekStackWord(0), str = PeekStackWord(2);
                if ((idx >> 8) != 0 || (idx & 0xFF) == 0 || (idx & 0xFF) > PM_MEM[str]) {  // IXS $99:
                    NO_WR16(PM_CPU.r.SP, 1);                   // index replaced by 1 (^string[1] left)
                    NO_WR16(PM_V(0x0246), PM_CPU.r.BC());            // SAVIPC
                    PM_CPU.r.PC = 0x03DA;                      // INVNDX (NativeErrors.inc)
                    break;
                }
                commit = true; PM_COUNT(OP_IXS);                 // stack is left exactly as it was
                break;
            }
            case OP_EFJ:                                         // jump if not equal
            case OP_NFJ: {                                       // jump if equal
                if (PM_PRESERVE) break;
                uint16_t bb = PeekStackWord(0), aa = PeekStackWord(2);
                bool jump = (nativeOpcodeLookup == OP_EFJ) ? (aa != bb) : (aa == bb);
                uint16_t ipc = PM_CPU.r.BC();
                uint8_t ofs = PM_MEM[ipc];
                PopStackWord(); PopStackWord();
                if (!jump) PM_CPU.r.setBC((uint16_t)(ipc + 1));                  // NOJ
                else if (!(ofs & 0x80)) PM_CPU.r.setBC((uint16_t)(ipc + 1 + ofs)); // UJP short
                else {                                                            // UJP long: jump table
                    uint16_t entry = (uint16_t)(NO_RD16(PM_V(0x02F4)) + ofs - 256);
                    PM_CPU.r.setBC((uint16_t)(entry - NO_RD16(entry)));
                }
                commit = true; PM_COUNT((uint8_t)nativeOpcodeLookup);
                break;
            }
            case OP_BPT: {                                       // conditional halt / breakpoint
                if (PM_PRESERVE) break;
                uint16_t ipc = PM_CPU.r.BC();
                uint8_t b0 = PM_MEM[ipc];
                uint16_t line = (b0 & 0x80) ? (uint16_t)(((b0 & 0x7F) << 8) | PM_MEM[(uint16_t)(ipc + 1)]) : b0; // GBDE
                uint16_t next = (uint16_t)(ipc + ((b0 & 0x80) ? 2 : 1));
                bool halt = (((uint8_t)(PM_MEM[PM_V(0x02EA)] - 3)) & 0x80) == 0;  // BUGSTA >= 3 (CP 3; JP P)
                for (int k = 0; k < 4 && !halt; k++)
                    if (NO_RD16(PM_V(0x02FE) + 2 * k) == line) halt = true;       // BRKPTS
                if (halt) {                                      // BPTHLT (NativeErrors.inc)
                    NO_WR16(PM_V(0x02FC), line);                       // HLTLINE
                    NO_WR16(PM_V(0x0246), next);                       // SAVIPC
                    PM_CPU.r.setBC(next);
                    PM_CPU.r.PC = 0x042E;
                    break;
                }
                PM_CPU.r.setBC(next);
                NO_WR16(PM_V(0x02FC), line);                           // HLTLINE
                NO_WR16(PM_V(0x0246), next);                           // SAVIPC
                commit = true; PM_COUNT(OP_BPT);
                break;
            }
#undef NO_RD16
#undef NO_WR16
#undef NO_PEEKREAL
#undef NO_PUSHREAL
#undef NO_FPDONE
