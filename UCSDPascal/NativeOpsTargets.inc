// NativeOpsTargets.inc -- routine entry addresses (XFRTBL targets) of the
// opcodes implemented in NativeOps.inc. Included inside each host's
// NativeOpcodeForTarget() switch.
        case 0x10A6: return OP_FLT;
        case 0x10AF: return OP_FLO;
        case 0x10C8: return OP_ABR;
        case 0x10D1: return OP_ADR;
        case 0x10DA: return OP_SBR;
        case 0x10E3: return OP_MPR;
        case 0x10EC: return OP_SQR;
        case 0x10F5: return OP_DVR;
        case 0x10FE: return OP_NGR;
        case 0x09C2: return OP_INT;
        case 0x076E: return OP_SQI;
        case 0x05F7: return OP_IXS;
        case 0x115F: return OP_EFJ;
        case 0x116E: return OP_NFJ;
        case 0x0433: return OP_BPT;
