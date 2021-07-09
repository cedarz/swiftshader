//===- subzero/src/IceInstX8632.cpp - X86-32 instruction implementation ---===//
//
//                        The Subzero Code Generator
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines X8632 specific data related to X8632 Instructions and
/// Instruction traits.
///
/// These are declared in the IceTargetLoweringX8632Traits.h header file. This
/// file also defines X8632 operand specific methods (dump and emit.)
///
//===----------------------------------------------------------------------===//

#include "IceInstX8632.h"

#include "IceAssemblerX8632.h"
#include "IceCfg.h"
#include "IceCfgNode.h"
#include "IceConditionCodesX86.h"
#include "IceDefs.h"
#include "IceInst.h"
#include "IceOperand.h"
#include "IceRegistersX8632.h"
#include "IceTargetLowering.h"
#include "IceTargetLoweringX8632.h"

namespace Ice {
namespace X8632 {

const char *InstX86Base::getWidthString(Type Ty) {
  return Traits::TypeAttributes[Ty].WidthString;
}

const char *InstX86Base::getFldString(Type Ty) {
  return Traits::TypeAttributes[Ty].FldString;
}

typename Cond::BrCond InstX86Base::getOppositeCondition(BrCond Cond) {
  return Traits::InstBrAttributes[Cond].Opposite;
}

InstX86FakeRMW::InstX86FakeRMW(Cfg *Func, Operand *Data, Operand *Addr,
                               InstArithmetic::OpKind Op, Variable *Beacon)
    : InstX86Base(Func, InstX86Base::FakeRMW, 3, nullptr), Op(Op) {
  this->addSource(Data);
  this->addSource(Addr);
  this->addSource(Beacon);
}

InstX86Mul::InstX86Mul(Cfg *Func, Variable *Dest, Variable *Source1,
                       Operand *Source2)
    : InstX86Base(Func, InstX86Base::Mul, 2, Dest) {
  this->addSource(Source1);
  this->addSource(Source2);
}

InstX86Shld::InstX86Shld(Cfg *Func, Variable *Dest, Variable *Source1,
                         Operand *Source2)
    : InstX86Base(Func, InstX86Base::Shld, 3, Dest) {
  this->addSource(Dest);
  this->addSource(Source1);
  this->addSource(Source2);
}

InstX86Shrd::InstX86Shrd(Cfg *Func, Variable *Dest, Variable *Source1,
                         Operand *Source2)
    : InstX86Base(Func, InstX86Base::Shrd, 3, Dest) {
  this->addSource(Dest);
  this->addSource(Source1);
  this->addSource(Source2);
}

InstX86Label::InstX86Label(Cfg *Func, TargetLowering *Target)
    : InstX86Base(Func, InstX86Base::Label, 0, nullptr),
      LabelNumber(Target->makeNextLabelNumber()) {
  if (BuildDefs::dump()) {
    Name = GlobalString::createWithString(
        Func->getContext(), ".L" + Func->getFunctionName() + "$local$__" +
                                std::to_string(LabelNumber));
  } else {
    Name = GlobalString::createWithoutString(Func->getContext());
  }
}

InstX86Br::InstX86Br(Cfg *Func, const CfgNode *TargetTrue,
                     const CfgNode *TargetFalse, const InstX86Label *Label,
                     BrCond Condition, Mode Kind)
    : InstX86Base(Func, InstX86Base::Br, 0, nullptr), Condition(Condition),
      TargetTrue(TargetTrue), TargetFalse(TargetFalse), Label(Label),
      Kind(Kind) {}

bool InstX86Br::optimizeBranch(const CfgNode *NextNode) {
  // If there is no next block, then there can be no fallthrough to optimize.
  if (NextNode == nullptr)
    return false;
  // Intra-block conditional branches can't be optimized.
  if (Label)
    return false;
  // If there is no fallthrough node, such as a non-default case label for a
  // switch instruction, then there is no opportunity to optimize.
  if (getTargetFalse() == nullptr)
    return false;

  // Unconditional branch to the next node can be removed.
  if (Condition == Cond::Br_None && getTargetFalse() == NextNode) {
    assert(getTargetTrue() == nullptr);
    this->setDeleted();
    return true;
  }
  // If the fallthrough is to the next node, set fallthrough to nullptr to
  // indicate.
  if (getTargetFalse() == NextNode) {
    TargetFalse = nullptr;
    return true;
  }
  // If TargetTrue is the next node, and TargetFalse is not nullptr (which was
  // already tested above), then invert the branch condition, swap the targets,
  // and set new fallthrough to nullptr.
  if (getTargetTrue() == NextNode) {
    assert(Condition != Cond::Br_None);
    Condition = this->getOppositeCondition(Condition);
    TargetTrue = getTargetFalse();
    TargetFalse = nullptr;
    return true;
  }
  return false;
}

bool InstX86Br::repointEdges(CfgNode *OldNode, CfgNode *NewNode) {
  bool Found = false;
  if (TargetFalse == OldNode) {
    TargetFalse = NewNode;
    Found = true;
  }
  if (TargetTrue == OldNode) {
    TargetTrue = NewNode;
    Found = true;
  }
  return Found;
}

InstX86Jmp::InstX86Jmp(Cfg *Func, Operand *Target)
    : InstX86Base(Func, InstX86Base::Jmp, 1, nullptr) {
  this->addSource(Target);
}

InstX86Call::InstX86Call(Cfg *Func, Variable *Dest, Operand *CallTarget)
    : InstX86Base(Func, InstX86Base::Call, 1, Dest) {
  this->HasSideEffects = true;
  this->addSource(CallTarget);
}

InstX86Movmsk::InstX86Movmsk(Cfg *Func, Variable *Dest, Operand *Source)
    : InstX86Base(Func, InstX86Base::Movmsk, 1, Dest) {
  this->addSource(Source);
}

InstX86Cmov::InstX86Cmov(Cfg *Func, Variable *Dest, Operand *Source,
                         BrCond Condition)
    : InstX86Base(Func, InstX86Base::Cmov, 2, Dest), Condition(Condition) {
  // The final result is either the original Dest, or Source, so mark both as
  // sources.
  this->addSource(Dest);
  this->addSource(Source);
}

InstX86Cmpps::InstX86Cmpps(Cfg *Func, Variable *Dest, Operand *Source,
                           CmppsCond Condition)
    : InstX86Base(Func, InstX86Base::Cmpps, 2, Dest), Condition(Condition) {
  this->addSource(Dest);
  this->addSource(Source);
}

InstX86Cmpxchg::InstX86Cmpxchg(Cfg *Func, Operand *DestOrAddr, Variable *Eax,
                               Variable *Desired, bool Locked)
    : InstX86BaseLockable(Func, InstX86Base::Cmpxchg, 3,
                          llvm::dyn_cast<Variable>(DestOrAddr), Locked) {
  constexpr uint16_t Encoded_rAX = 0;
  (void)Encoded_rAX;
  assert(Traits::getEncodedGPR(Eax->getRegNum()) == Encoded_rAX);
  this->addSource(DestOrAddr);
  this->addSource(Eax);
  this->addSource(Desired);
}

InstX86Cmpxchg8b::InstX86Cmpxchg8b(Cfg *Func, X86OperandMem *Addr,
                                   Variable *Edx, Variable *Eax, Variable *Ecx,
                                   Variable *Ebx, bool Locked)
    : InstX86BaseLockable(Func, InstX86Base::Cmpxchg, 5, nullptr, Locked) {
  assert(Edx->getRegNum() == RegisterSet::Reg_edx);
  assert(Eax->getRegNum() == RegisterSet::Reg_eax);
  assert(Ecx->getRegNum() == RegisterSet::Reg_ecx);
  assert(Ebx->getRegNum() == RegisterSet::Reg_ebx);
  this->addSource(Addr);
  this->addSource(Edx);
  this->addSource(Eax);
  this->addSource(Ecx);
  this->addSource(Ebx);
}

InstX86Cvt::InstX86Cvt(Cfg *Func, Variable *Dest, Operand *Source,
                       CvtVariant Variant)
    : InstX86Base(Func, InstX86Base::Cvt, 1, Dest), Variant(Variant) {
  this->addSource(Source);
}

InstX86Icmp::InstX86Icmp(Cfg *Func, Operand *Src0, Operand *Src1)
    : InstX86Base(Func, InstX86Base::Icmp, 2, nullptr) {
  this->addSource(Src0);
  this->addSource(Src1);
}

InstX86Ucomiss::InstX86Ucomiss(Cfg *Func, Operand *Src0, Operand *Src1)
    : InstX86Base(Func, InstX86Base::Ucomiss, 2, nullptr) {
  this->addSource(Src0);
  this->addSource(Src1);
}

InstX86UD2::InstX86UD2(Cfg *Func)
    : InstX86Base(Func, InstX86Base::UD2, 0, nullptr) {}

InstX86Int3::InstX86Int3(Cfg *Func)
    : InstX86Base(Func, InstX86Base::Int3, 0, nullptr) {}

InstX86Test::InstX86Test(Cfg *Func, Operand *Src1, Operand *Src2)
    : InstX86Base(Func, InstX86Base::Test, 2, nullptr) {
  this->addSource(Src1);
  this->addSource(Src2);
}

InstX86Mfence::InstX86Mfence(Cfg *Func)
    : InstX86Base(Func, InstX86Base::Mfence, 0, nullptr) {
  this->HasSideEffects = true;
}

InstX86Store::InstX86Store(Cfg *Func, Operand *Value, X86Operand *Mem)
    : InstX86Base(Func, InstX86Base::Store, 2, nullptr) {
  this->addSource(Value);
  this->addSource(Mem);
}

InstX86StoreP::InstX86StoreP(Cfg *Func, Variable *Value, X86OperandMem *Mem)
    : InstX86Base(Func, InstX86Base::StoreP, 2, nullptr) {
  this->addSource(Value);
  this->addSource(Mem);
}

InstX86StoreQ::InstX86StoreQ(Cfg *Func, Operand *Value, X86OperandMem *Mem)
    : InstX86Base(Func, InstX86Base::StoreQ, 2, nullptr) {
  this->addSource(Value);
  this->addSource(Mem);
}

InstX86StoreD::InstX86StoreD(Cfg *Func, Operand *Value, X86OperandMem *Mem)
    : InstX86Base(Func, InstX86Base::StoreD, 2, nullptr) {
  this->addSource(Value);
  this->addSource(Mem);
}

InstX86Nop::InstX86Nop(Cfg *Func, NopVariant Variant)
    : InstX86Base(Func, InstX86Base::Nop, 0, nullptr), Variant(Variant) {}

InstX86Fld::InstX86Fld(Cfg *Func, Operand *Src)
    : InstX86Base(Func, InstX86Base::Fld, 1, nullptr) {
  this->addSource(Src);
}

InstX86Fstp::InstX86Fstp(Cfg *Func, Variable *Dest)
    : InstX86Base(Func, InstX86Base::Fstp, 0, Dest) {}

InstX86Pop::InstX86Pop(Cfg *Func, Variable *Dest)
    : InstX86Base(Func, InstX86Base::Pop, 0, Dest) {
  // A pop instruction affects the stack pointer and so it should not be
  // allowed to be automatically dead-code eliminated. (The corresponding push
  // instruction doesn't need this treatment because it has no dest variable
  // and therefore won't be dead-code eliminated.) This is needed for
  // late-stage liveness analysis (e.g. asm-verbose mode).
  this->HasSideEffects = true;
}

InstX86Push::InstX86Push(Cfg *Func, Operand *Source)
    : InstX86Base(Func, InstX86Base::Push, 1, nullptr) {
  this->addSource(Source);
}

InstX86Ret::InstX86Ret(Cfg *Func, Variable *Source)
    : InstX86Base(Func, InstX86Base::Ret, Source ? 1 : 0, nullptr) {
  if (Source)
    this->addSource(Source);
}

InstX86Setcc::InstX86Setcc(Cfg *Func, Variable *Dest, BrCond Cond)
    : InstX86Base(Func, InstX86Base::Setcc, 0, Dest), Condition(Cond) {}

InstX86Xadd::InstX86Xadd(Cfg *Func, Operand *Dest, Variable *Source,
                         bool Locked)
    : InstX86BaseLockable(Func, InstX86Base::Xadd, 2,
                          llvm::dyn_cast<Variable>(Dest), Locked) {
  this->addSource(Dest);
  this->addSource(Source);
}

InstX86Xchg::InstX86Xchg(Cfg *Func, Operand *Dest, Variable *Source)
    : InstX86Base(Func, InstX86Base::Xchg, 2, llvm::dyn_cast<Variable>(Dest)) {
  this->addSource(Dest);
  this->addSource(Source);
}

InstX86IacaStart::InstX86IacaStart(Cfg *Func)
    : InstX86Base(Func, InstX86Base::IacaStart, 0, nullptr) {
  assert(getFlags().getAllowIacaMarks());
}

InstX86IacaEnd::InstX86IacaEnd(Cfg *Func)
    : InstX86Base(Func, InstX86Base::IacaEnd, 0, nullptr) {
  assert(getFlags().getAllowIacaMarks());
}

// ======================== Dump routines ======================== //

void InstX86Base::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "[" << Traits::TargetName << "] ";
  Inst::dump(Func);
}

void InstX86FakeRMW::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Type Ty = getData()->getType();
  Str << "rmw " << InstArithmetic::getOpName(getOp()) << " " << Ty << " *";
  getAddr()->dump(Func);
  Str << ", ";
  getData()->dump(Func);
  Str << ", beacon=";
  getBeacon()->dump(Func);
}

void InstX86Label::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  Str << getLabelName() << ":";
}

void InstX86Label::emitIAS(const Cfg *Func) const {
  Assembler *Asm = Func->getAssembler<Assembler>();
  Asm->bindLocalLabel(LabelNumber);
  if (OffsetReloc != nullptr) {
    Asm->bindRelocOffset(OffsetReloc);
  }
}

void InstX86Label::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << getLabelName() << ":";
}

void InstX86Br::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  Str << "\t";

  if (Condition == Cond::Br_None) {
    Str << "jmp";
  } else {
    Str << Traits::InstBrAttributes[Condition].EmitString;
  }

  if (Label) {
    Str << "\t" << Label->getLabelName();
  } else {
    if (Condition == Cond::Br_None) {
      Str << "\t" << getTargetFalse()->getAsmName();
    } else {
      Str << "\t" << getTargetTrue()->getAsmName();
      if (getTargetFalse()) {
        Str << "\n\t"
               "jmp\t"
            << getTargetFalse()->getAsmName();
      }
    }
  }
}

void InstX86Br::emitIAS(const Cfg *Func) const {
  Assembler *Asm = Func->getAssembler<Assembler>();
  if (Label) {
    auto *L = Asm->getOrCreateLocalLabel(Label->getLabelNumber());
    if (Condition == Cond::Br_None) {
      Asm->jmp(L, isNear());
    } else {
      Asm->j(Condition, L, isNear());
    }
  } else {
    if (Condition == Cond::Br_None) {
      auto *L = Asm->getOrCreateCfgNodeLabel(getTargetFalse()->getIndex());
      assert(!getTargetTrue());
      Asm->jmp(L, isNear());
    } else {
      auto *L = Asm->getOrCreateCfgNodeLabel(getTargetTrue()->getIndex());
      Asm->j(Condition, L, isNear());
      if (getTargetFalse()) {
        auto *L2 = Asm->getOrCreateCfgNodeLabel(getTargetFalse()->getIndex());
        Asm->jmp(L2, isNear());
      }
    }
  }
}

void InstX86Br::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "br ";

  if (Condition == Cond::Br_None) {
    if (Label) {
      Str << "label %" << Label->getLabelName();
    } else {
      Str << "label %" << getTargetFalse()->getName();
    }
    return;
  }

  Str << Traits::InstBrAttributes[Condition].DisplayString;
  if (Label) {
    Str << ", label %" << Label->getLabelName();
  } else {
    Str << ", label %" << getTargetTrue()->getName();
    if (getTargetFalse()) {
      Str << ", label %" << getTargetFalse()->getName();
    }
  }

  Str << " // (" << (isNear() ? "near" : "far") << " jump)";
}

void InstX86Jmp::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 1);
  const Operand *Src = this->getSrc(0);
  if (Traits::Is64Bit) {
    if (const auto *CR = llvm::dyn_cast<ConstantRelocatable>(Src)) {
      Str << "\t"
             "jmp"
             "\t"
          << CR->getName();
      return;
    }
  }
  Str << "\t"
         "jmp"
         "\t*";
  getJmpTarget()->emit(Func);
}

void InstX86Jmp::emitIAS(const Cfg *Func) const {
  // Note: Adapted (mostly copied) from
  // InstX86Call::emitIAS().
  Assembler *Asm = Func->getAssembler<Assembler>();
  Operand *Target = getJmpTarget();
  if (const auto *Var = llvm::dyn_cast<Variable>(Target)) {
    if (Var->hasReg()) {
      Asm->jmp(Traits::getEncodedGPR(Var->getRegNum()));
    } else {
      // The jmp instruction with a memory operand should be possible to
      // encode, but it isn't a valid sandboxed instruction, and there
      // shouldn't be a register allocation issue to jump through a scratch
      // register, so we don't really need to bother implementing it.
      llvm::report_fatal_error("Assembler can't jmp to memory operand");
    }
  } else if (const auto *Mem = llvm::dyn_cast<X86OperandMem>(Target)) {
    (void)Mem;
    assert(Mem->getSegmentRegister() == X86OperandMem::DefaultSegment);
    llvm::report_fatal_error("Assembler can't jmp to memory operand");
  } else if (const auto *CR = llvm::dyn_cast<ConstantRelocatable>(Target)) {
    Asm->jmp(CR);
  } else if (const auto *Imm = llvm::dyn_cast<ConstantInteger32>(Target)) {
    // NaCl trampoline calls refer to an address within the sandbox directly.
    // This is usually only needed for non-IRT builds and otherwise not very
    // portable or stable. Usually this is only done for "calls" and not jumps.
    Asm->jmp(AssemblerImmediate(Imm->getValue()));
  } else {
    llvm::report_fatal_error("Unexpected operand type");
  }
}

void InstX86Jmp::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "jmp ";
  getJmpTarget()->dump(Func);
}

void InstX86Call::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 1);
  Str << "\t"
         "call\t";
  Operand *CallTarget = getCallTarget();
  auto *Target = InstX86Base::getTarget(Func);
  if (const auto *CI = llvm::dyn_cast<ConstantInteger32>(CallTarget)) {
    // Emit without a leading '$'.
    Str << CI->getValue();
  } else if (const auto DirectCallTarget =
                 llvm::dyn_cast<ConstantRelocatable>(CallTarget)) {
    DirectCallTarget->emitWithoutPrefix(Target);
  } else {
    Str << "*";
    CallTarget->emit(Func);
  }
}

void InstX86Call::emitIAS(const Cfg *Func) const {
  Assembler *Asm = Func->getAssembler<Assembler>();
  Operand *CallTarget = getCallTarget();
  auto *Target = InstX86Base::getTarget(Func);
  if (const auto *Var = llvm::dyn_cast<Variable>(CallTarget)) {
    if (Var->hasReg()) {
      Asm->call(Traits::getEncodedGPR(Var->getRegNum()));
    } else {
      Asm->call(Target->stackVarToAsmOperand(Var));
    }
  } else if (const auto *Mem = llvm::dyn_cast<X86OperandMem>(CallTarget)) {
    assert(Mem->getSegmentRegister() == X86OperandMem::DefaultSegment);
    Asm->call(Mem->toAsmAddress(Asm, Target));
  } else if (const auto *CR = llvm::dyn_cast<ConstantRelocatable>(CallTarget)) {
    Asm->call(CR);
  } else if (const auto *Imm = llvm::dyn_cast<ConstantInteger32>(CallTarget)) {
    Asm->call(AssemblerImmediate(Imm->getValue()));
  } else {
    llvm_unreachable("Unexpected operand type");
  }
}

void InstX86Call::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  if (this->getDest()) {
    this->dumpDest(Func);
    Str << " = ";
  }
  Str << "call ";
  getCallTarget()->dump(Func);
}

// The this->Opcode parameter needs to be char* and not std::string because of
// template issues.

void InstX86Base::emitTwoAddress(const Cfg *Func, const char *Opcode,
                                 const char *Suffix) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(getSrcSize() == 2);
  Operand *Dest = getDest();
  if (Dest == nullptr)
    Dest = getSrc(0);
  assert(Dest == getSrc(0));
  Operand *Src1 = getSrc(1);
  Str << "\t" << Opcode << Suffix
      << InstX86Base::getWidthString(Dest->getType()) << "\t";
  Src1->emit(Func);
  Str << ", ";
  Dest->emit(Func);
}

void emitIASOpTyGPR(const Cfg *Func, Type Ty, const Operand *Op,
                    const GPREmitterOneOp &Emitter) {
  auto *Target = InstX86Base::getTarget(Func);
  Assembler *Asm = Func->getAssembler<Assembler>();
  if (const auto *Var = llvm::dyn_cast<Variable>(Op)) {
    if (Var->hasReg()) {
      // We cheat a little and use GPRRegister even for byte operations.
      GPRRegister VarReg = Traits::getEncodedGPR(Var->getRegNum());
      (Asm->*(Emitter.Reg))(Ty, VarReg);
    } else {
      Address StackAddr(Target->stackVarToAsmOperand(Var));
      (Asm->*(Emitter.Addr))(Ty, StackAddr);
    }
  } else if (const auto *Mem = llvm::dyn_cast<X86OperandMem>(Op)) {
    Mem->emitSegmentOverride(Asm);
    (Asm->*(Emitter.Addr))(Ty, Mem->toAsmAddress(Asm, Target));
  } else {
    llvm_unreachable("Unexpected operand type");
  }
}

template <bool VarCanBeByte, bool SrcCanBeByte>
void emitIASRegOpTyGPR(const Cfg *Func, bool IsLea, Type Ty,
                       const Variable *Var, const Operand *Src,
                       const GPREmitterRegOp &Emitter) {
  auto *Target = InstX86Base::getTarget(Func);
  Assembler *Asm = Func->getAssembler<Assembler>();
  assert(Var->hasReg());
  // We cheat a little and use GPRRegister even for byte operations.
  GPRRegister VarReg = VarCanBeByte ? Traits::getEncodedGPR(Var->getRegNum())
                                    : Traits::getEncodedGPR(Var->getRegNum());
  if (const auto *SrcVar = llvm::dyn_cast<Variable>(Src)) {
    if (SrcVar->hasReg()) {
      GPRRegister SrcReg = SrcCanBeByte
                               ? Traits::getEncodedGPR(SrcVar->getRegNum())
                               : Traits::getEncodedGPR(SrcVar->getRegNum());
      (Asm->*(Emitter.GPRGPR))(Ty, VarReg, SrcReg);
    } else {
      Address SrcStackAddr = Target->stackVarToAsmOperand(SrcVar);
      (Asm->*(Emitter.GPRAddr))(Ty, VarReg, SrcStackAddr);
    }
  } else if (const auto *Mem = llvm::dyn_cast<X86OperandMem>(Src)) {
    Mem->emitSegmentOverride(Asm);
    (Asm->*(Emitter.GPRAddr))(Ty, VarReg,
                              Mem->toAsmAddress(Asm, Target, IsLea));
  } else if (const auto *Imm = llvm::dyn_cast<ConstantInteger32>(Src)) {
    (Asm->*(Emitter.GPRImm))(Ty, VarReg, AssemblerImmediate(Imm->getValue()));
  } else if (const auto *Imm = llvm::dyn_cast<ConstantInteger64>(Src)) {
    assert(Traits::Is64Bit);
    assert(Utils::IsInt(32, Imm->getValue()));
    (Asm->*(Emitter.GPRImm))(Ty, VarReg, AssemblerImmediate(Imm->getValue()));
  } else if (const auto *Reloc = llvm::dyn_cast<ConstantRelocatable>(Src)) {
    const auto FixupKind = (Reloc->getName().hasStdString() &&
                            Reloc->getName().toString() == GlobalOffsetTable)
                               ? Traits::FK_GotPC
                               : Traits::TargetLowering::getAbsFixup();
    AssemblerFixup *Fixup = Asm->createFixup(FixupKind, Reloc);
    (Asm->*(Emitter.GPRImm))(Ty, VarReg, AssemblerImmediate(Fixup));
  } else if (const auto *Split = llvm::dyn_cast<VariableSplit>(Src)) {
    (Asm->*(Emitter.GPRAddr))(Ty, VarReg, Split->toAsmAddress(Func));
  } else {
    llvm_unreachable("Unexpected operand type");
  }
}

void emitIASAddrOpTyGPR(const Cfg *Func, Type Ty, const Address &Addr,
                        const Operand *Src, const GPREmitterAddrOp &Emitter) {
  Assembler *Asm = Func->getAssembler<Assembler>();
  // Src can only be Reg or AssemblerImmediate.
  if (const auto *SrcVar = llvm::dyn_cast<Variable>(Src)) {
    assert(SrcVar->hasReg());
    GPRRegister SrcReg = Traits::getEncodedGPR(SrcVar->getRegNum());
    (Asm->*(Emitter.AddrGPR))(Ty, Addr, SrcReg);
  } else if (const auto *Imm = llvm::dyn_cast<ConstantInteger32>(Src)) {
    (Asm->*(Emitter.AddrImm))(Ty, Addr, AssemblerImmediate(Imm->getValue()));
  } else if (const auto *Imm = llvm::dyn_cast<ConstantInteger64>(Src)) {
    assert(Traits::Is64Bit);
    assert(Utils::IsInt(32, Imm->getValue()));
    (Asm->*(Emitter.AddrImm))(Ty, Addr, AssemblerImmediate(Imm->getValue()));
  } else if (const auto *Reloc = llvm::dyn_cast<ConstantRelocatable>(Src)) {
    const auto FixupKind = (Reloc->getName().hasStdString() &&
                            Reloc->getName().toString() == GlobalOffsetTable)
                               ? Traits::FK_GotPC
                               : Traits::TargetLowering::getAbsFixup();
    AssemblerFixup *Fixup = Asm->createFixup(FixupKind, Reloc);
    (Asm->*(Emitter.AddrImm))(Ty, Addr, AssemblerImmediate(Fixup));
  } else {
    llvm_unreachable("Unexpected operand type");
  }
}

void emitIASAsAddrOpTyGPR(const Cfg *Func, Type Ty, const Operand *Op0,
                          const Operand *Op1, const GPREmitterAddrOp &Emitter) {
  auto *Target = InstX86Base::getTarget(Func);
  if (const auto *Op0Var = llvm::dyn_cast<Variable>(Op0)) {
    assert(!Op0Var->hasReg());
    Address StackAddr(Target->stackVarToAsmOperand(Op0Var));
    emitIASAddrOpTyGPR(Func, Ty, StackAddr, Op1, Emitter);
  } else if (const auto *Op0Mem = llvm::dyn_cast<X86OperandMem>(Op0)) {
    Assembler *Asm = Func->getAssembler<Assembler>();
    Op0Mem->emitSegmentOverride(Asm);
    emitIASAddrOpTyGPR(Func, Ty, Op0Mem->toAsmAddress(Asm, Target), Op1,
                       Emitter);
  } else if (const auto *Split = llvm::dyn_cast<VariableSplit>(Op0)) {
    emitIASAddrOpTyGPR(Func, Ty, Split->toAsmAddress(Func), Op1, Emitter);
  } else {
    llvm_unreachable("Unexpected operand type");
  }
}

void emitIASGPRShift(const Cfg *Func, Type Ty, const Variable *Var,
                     const Operand *Src, const GPREmitterShiftOp &Emitter) {
  Assembler *Asm = Func->getAssembler<Assembler>();
  // Technically, the Dest Var can be mem as well, but we only use Reg. We can
  // extend this to check Dest if we decide to use that form.
  assert(Var->hasReg());
  // We cheat a little and use GPRRegister even for byte operations.
  GPRRegister VarReg = Traits::getEncodedGPR(Var->getRegNum());
  // Src must be reg == ECX or an Imm8. This is asserted by the assembler.
  if (const auto *SrcVar = llvm::dyn_cast<Variable>(Src)) {
    assert(SrcVar->hasReg());
    GPRRegister SrcReg = Traits::getEncodedGPR(SrcVar->getRegNum());
    (Asm->*(Emitter.GPRGPR))(Ty, VarReg, SrcReg);
  } else if (const auto *Imm = llvm::dyn_cast<ConstantInteger32>(Src)) {
    (Asm->*(Emitter.GPRImm))(Ty, VarReg, AssemblerImmediate(Imm->getValue()));
  } else if (const auto *Imm = llvm::dyn_cast<ConstantInteger64>(Src)) {
    assert(Traits::Is64Bit);
    assert(Utils::IsInt(32, Imm->getValue()));
    (Asm->*(Emitter.GPRImm))(Ty, VarReg, AssemblerImmediate(Imm->getValue()));
  } else {
    llvm_unreachable("Unexpected operand type");
  }
}

void emitIASGPRShiftDouble(const Cfg *Func, const Variable *Dest,
                           const Operand *Src1Op, const Operand *Src2Op,
                           const GPREmitterShiftD &Emitter) {
  Assembler *Asm = Func->getAssembler<Assembler>();
  // Dest can be reg or mem, but we only use the reg variant.
  assert(Dest->hasReg());
  GPRRegister DestReg = Traits::getEncodedGPR(Dest->getRegNum());
  // SrcVar1 must be reg.
  const auto *SrcVar1 = llvm::cast<Variable>(Src1Op);
  assert(SrcVar1->hasReg());
  GPRRegister SrcReg = Traits::getEncodedGPR(SrcVar1->getRegNum());
  Type Ty = SrcVar1->getType();
  // Src2 can be the implicit CL register or an immediate.
  if (const auto *Imm = llvm::dyn_cast<ConstantInteger32>(Src2Op)) {
    (Asm->*(Emitter.GPRGPRImm))(Ty, DestReg, SrcReg,
                                AssemblerImmediate(Imm->getValue()));
  } else {
    assert(llvm::cast<Variable>(Src2Op)->getRegNum() == RegisterSet::Reg_cl);
    (Asm->*(Emitter.GPRGPR))(Ty, DestReg, SrcReg);
  }
}

void emitIASXmmShift(const Cfg *Func, Type Ty, const Variable *Var,
                     const Operand *Src, const XmmEmitterShiftOp &Emitter) {
  auto *Target = InstX86Base::getTarget(Func);
  Assembler *Asm = Func->getAssembler<Assembler>();
  assert(Var->hasReg());
  XmmRegister VarReg = Traits::getEncodedXmm(Var->getRegNum());
  if (const auto *SrcVar = llvm::dyn_cast<Variable>(Src)) {
    if (SrcVar->hasReg()) {
      XmmRegister SrcReg = Traits::getEncodedXmm(SrcVar->getRegNum());
      (Asm->*(Emitter.XmmXmm))(Ty, VarReg, SrcReg);
    } else {
      Address SrcStackAddr = Target->stackVarToAsmOperand(SrcVar);
      (Asm->*(Emitter.XmmAddr))(Ty, VarReg, SrcStackAddr);
    }
  } else if (const auto *Mem = llvm::dyn_cast<X86OperandMem>(Src)) {
    assert(Mem->getSegmentRegister() == X86OperandMem::DefaultSegment);
    (Asm->*(Emitter.XmmAddr))(Ty, VarReg, Mem->toAsmAddress(Asm, Target));
  } else if (const auto *Imm = llvm::dyn_cast<ConstantInteger32>(Src)) {
    (Asm->*(Emitter.XmmImm))(Ty, VarReg, AssemblerImmediate(Imm->getValue()));
  } else {
    llvm_unreachable("Unexpected operand type");
  }
}

void emitIASRegOpTyXMM(const Cfg *Func, Type Ty, const Variable *Var,
                       const Operand *Src, const XmmEmitterRegOp &Emitter) {
  auto *Target = InstX86Base::getTarget(Func);
  Assembler *Asm = Func->getAssembler<Assembler>();
  assert(Var->hasReg());
  XmmRegister VarReg = Traits::getEncodedXmm(Var->getRegNum());
  if (const auto *SrcVar = llvm::dyn_cast<Variable>(Src)) {
    if (SrcVar->hasReg()) {
      XmmRegister SrcReg = Traits::getEncodedXmm(SrcVar->getRegNum());
      (Asm->*(Emitter.XmmXmm))(Ty, VarReg, SrcReg);
    } else {
      Address SrcStackAddr = Target->stackVarToAsmOperand(SrcVar);
      (Asm->*(Emitter.XmmAddr))(Ty, VarReg, SrcStackAddr);
    }
  } else if (const auto *Mem = llvm::dyn_cast<X86OperandMem>(Src)) {
    assert(Mem->getSegmentRegister() == X86OperandMem::DefaultSegment);
    (Asm->*(Emitter.XmmAddr))(Ty, VarReg, Mem->toAsmAddress(Asm, Target));
  } else if (const auto *Imm = llvm::dyn_cast<Constant>(Src)) {
    (Asm->*(Emitter.XmmAddr))(Ty, VarReg,
                              Traits::Address::ofConstPool(Asm, Imm));
  } else {
    llvm_unreachable("Unexpected operand type");
  }
}

template <typename DReg_t, typename SReg_t, DReg_t (*destEnc)(RegNumT),
          SReg_t (*srcEnc)(RegNumT)>
void emitIASCastRegOp(const Cfg *Func, Type DestTy, const Variable *Dest,
                      Type SrcTy, const Operand *Src,
                      const CastEmitterRegOp<DReg_t, SReg_t> &Emitter) {
  auto *Target = InstX86Base::getTarget(Func);
  Assembler *Asm = Func->getAssembler<Assembler>();
  assert(Dest->hasReg());
  DReg_t DestReg = destEnc(Dest->getRegNum());
  if (const auto *SrcVar = llvm::dyn_cast<Variable>(Src)) {
    if (SrcVar->hasReg()) {
      SReg_t SrcReg = srcEnc(SrcVar->getRegNum());
      (Asm->*(Emitter.RegReg))(DestTy, DestReg, SrcTy, SrcReg);
    } else {
      Address SrcStackAddr = Target->stackVarToAsmOperand(SrcVar);
      (Asm->*(Emitter.RegAddr))(DestTy, DestReg, SrcTy, SrcStackAddr);
    }
  } else if (const auto *Mem = llvm::dyn_cast<X86OperandMem>(Src)) {
    Mem->emitSegmentOverride(Asm);
    (Asm->*(Emitter.RegAddr))(DestTy, DestReg, SrcTy,
                              Mem->toAsmAddress(Asm, Target));
  } else {
    llvm_unreachable("Unexpected operand type");
  }
}

template <typename DReg_t, typename SReg_t, DReg_t (*destEnc)(RegNumT),
          SReg_t (*srcEnc)(RegNumT)>
void emitIASThreeOpImmOps(const Cfg *Func, Type DispatchTy,
                          const Variable *Dest, const Operand *Src0,
                          const Operand *Src1,
                          const ThreeOpImmEmitter<DReg_t, SReg_t> Emitter) {
  auto *Target = InstX86Base::getTarget(Func);
  Assembler *Asm = Func->getAssembler<Assembler>();
  // This only handles Dest being a register, and Src1 being an immediate.
  assert(Dest->hasReg());
  DReg_t DestReg = destEnc(Dest->getRegNum());
  AssemblerImmediate Imm(llvm::cast<ConstantInteger32>(Src1)->getValue());
  if (const auto *SrcVar = llvm::dyn_cast<Variable>(Src0)) {
    if (SrcVar->hasReg()) {
      SReg_t SrcReg = srcEnc(SrcVar->getRegNum());
      (Asm->*(Emitter.RegRegImm))(DispatchTy, DestReg, SrcReg, Imm);
    } else {
      Address SrcStackAddr = Target->stackVarToAsmOperand(SrcVar);
      (Asm->*(Emitter.RegAddrImm))(DispatchTy, DestReg, SrcStackAddr, Imm);
    }
  } else if (const auto *Mem = llvm::dyn_cast<X86OperandMem>(Src0)) {
    Mem->emitSegmentOverride(Asm);
    (Asm->*(Emitter.RegAddrImm))(DispatchTy, DestReg,
                                 Mem->toAsmAddress(Asm, Target), Imm);
  } else {
    llvm_unreachable("Unexpected operand type");
  }
}

void emitIASMovlikeXMM(const Cfg *Func, const Variable *Dest,
                       const Operand *Src, const XmmEmitterMovOps Emitter) {
  auto *Target = InstX86Base::getTarget(Func);
  Assembler *Asm = Func->getAssembler<Assembler>();
  if (Dest->hasReg()) {
    XmmRegister DestReg = Traits::getEncodedXmm(Dest->getRegNum());
    if (const auto *SrcVar = llvm::dyn_cast<Variable>(Src)) {
      if (SrcVar->hasReg()) {
        (Asm->*(Emitter.XmmXmm))(DestReg,
                                 Traits::getEncodedXmm(SrcVar->getRegNum()));
      } else {
        Address StackAddr(Target->stackVarToAsmOperand(SrcVar));
        (Asm->*(Emitter.XmmAddr))(DestReg, StackAddr);
      }
    } else if (const auto *SrcMem = llvm::dyn_cast<X86OperandMem>(Src)) {
      assert(SrcMem->getSegmentRegister() == X86OperandMem::DefaultSegment);
      (Asm->*(Emitter.XmmAddr))(DestReg, SrcMem->toAsmAddress(Asm, Target));
    } else {
      llvm_unreachable("Unexpected operand type");
    }
  } else {
    Address StackAddr(Target->stackVarToAsmOperand(Dest));
    // Src must be a register in this case.
    const auto *SrcVar = llvm::cast<Variable>(Src);
    assert(SrcVar->hasReg());
    (Asm->*(Emitter.AddrXmm))(StackAddr,
                              Traits::getEncodedXmm(SrcVar->getRegNum()));
  }
}

void InstX86Movmsk::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  this->dumpDest(Func);
  Str << " = movmsk." << this->getSrc(0)->getType() << " ";
  this->dumpSources(Func);
}

void InstX86Movmsk::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 1);
  Type SrcTy = this->getSrc(0)->getType();
  assert(isVectorType(SrcTy));
  switch (SrcTy) {
  case IceType_v16i8:
    Str << "\t"
           "pmovmskb"
           "\t";
    break;
  case IceType_v4i32:
  case IceType_v4f32:
    Str << "\t"
           "movmskps"
           "\t";
    break;
  default:
    llvm_unreachable("Unexpected operand type");
  }
  this->getSrc(0)->emit(Func);
  Str << ", ";
  this->getDest()->emit(Func);
}

void InstX86Movmsk::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 1);
  Assembler *Asm = Func->getAssembler<Assembler>();
  const Variable *Dest = this->getDest();
  const Variable *Src = llvm::cast<Variable>(this->getSrc(0));
  const Type DestTy = Dest->getType();
  (void)DestTy;
  const Type SrcTy = Src->getType();
  assert(isVectorType(SrcTy));
  assert(isScalarIntegerType(DestTy));
  if (Traits::Is64Bit) {
    assert(DestTy == IceType_i32 || DestTy == IceType_i64);
  } else {
    assert(typeWidthInBytes(DestTy) <= 4);
  }
  XmmRegister SrcReg = Traits::getEncodedXmm(Src->getRegNum());
  GPRRegister DestReg = Traits::getEncodedGPR(Dest->getRegNum());
  Asm->movmsk(SrcTy, DestReg, SrcReg);
}

void InstX86Sqrt::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 1);
  Type Ty = this->getSrc(0)->getType();
  assert(isScalarFloatingType(Ty));
  Str << "\t"
         "sqrt"
      << Traits::TypeAttributes[Ty].SpSdString << "\t";
  this->getSrc(0)->emit(Func);
  Str << ", ";
  this->getDest()->emit(Func);
}

void InstX86Div::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 3);
  Operand *Src1 = this->getSrc(1);
  Str << "\t" << this->Opcode << this->getWidthString(Src1->getType()) << "\t";
  Src1->emit(Func);
}

void InstX86Div::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 3);
  const Operand *Src = this->getSrc(1);
  Type Ty = Src->getType();
  static GPREmitterOneOp Emitter = {&Assembler::div, &Assembler::div};
  emitIASOpTyGPR(Func, Ty, Src, Emitter);
}

void InstX86Idiv::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 3);
  Operand *Src1 = this->getSrc(1);
  Str << "\t" << this->Opcode << this->getWidthString(Src1->getType()) << "\t";
  Src1->emit(Func);
}

void InstX86Idiv::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 3);
  const Operand *Src = this->getSrc(1);
  Type Ty = Src->getType();
  static const GPREmitterOneOp Emitter = {&Assembler::idiv, &Assembler::idiv};
  emitIASOpTyGPR(Func, Ty, Src, Emitter);
}

// pblendvb and blendvps take xmm0 as a final implicit argument.

void emitVariableBlendInst(const char *Opcode, const Inst *Instr,
                           const Cfg *Func) {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(Instr->getSrcSize() == 3);
  assert(llvm::cast<Variable>(Instr->getSrc(2))->getRegNum() ==
         RegisterSet::Reg_xmm0);
  Str << "\t" << Opcode << "\t";
  Instr->getSrc(1)->emit(Func);
  Str << ", ";
  Instr->getDest()->emit(Func);
}

void emitIASVariableBlendInst(const Inst *Instr, const Cfg *Func,
                              const XmmEmitterRegOp &Emitter) {
  assert(Instr->getSrcSize() == 3);
  assert(llvm::cast<Variable>(Instr->getSrc(2))->getRegNum() ==
         RegisterSet::Reg_xmm0);
  const Variable *Dest = Instr->getDest();
  const Operand *Src = Instr->getSrc(1);
  emitIASRegOpTyXMM(Func, Dest->getType(), Dest, Src, Emitter);
}

void InstX86Blendvps::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  emitVariableBlendInst(this->Opcode, this, Func);
}

void InstX86Blendvps::emitIAS(const Cfg *Func) const {
  static const XmmEmitterRegOp Emitter = {&Assembler::blendvps,
                                          &Assembler::blendvps};
  emitIASVariableBlendInst(this, Func, Emitter);
}

void InstX86Pblendvb::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  emitVariableBlendInst(this->Opcode, this, Func);
}

void InstX86Pblendvb::emitIAS(const Cfg *Func) const {
  static const XmmEmitterRegOp Emitter = {&Assembler::pblendvb,
                                          &Assembler::pblendvb};
  emitIASVariableBlendInst(this, Func, Emitter);
}

void InstX86Imul::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 2);
  Variable *Dest = this->getDest();
  if (isByteSizedArithType(Dest->getType())) {
    // The 8-bit version of imul only allows the form "imul r/m8".
    const auto *Src0Var = llvm::dyn_cast<Variable>(this->getSrc(0));
    (void)Src0Var;
    assert(Src0Var->getRegNum() == RegisterSet::Reg_al);
    Str << "\t"
           "imulb\t";
    this->getSrc(1)->emit(Func);
  } else if (llvm::isa<Constant>(this->getSrc(1))) {
    Str << "\t"
           "imul"
        << this->getWidthString(Dest->getType()) << "\t";
    this->getSrc(1)->emit(Func);
    Str << ", ";
    this->getSrc(0)->emit(Func);
    Str << ", ";
    Dest->emit(Func);
  } else {
    this->emitTwoAddress(Func, this->Opcode);
  }
}

void InstX86Imul::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 2);
  const Variable *Var = this->getDest();
  Type Ty = Var->getType();
  const Operand *Src = this->getSrc(1);
  if (isByteSizedArithType(Ty)) {
    // The 8-bit version of imul only allows the form "imul r/m8".
    const auto *Src0Var = llvm::dyn_cast<Variable>(this->getSrc(0));
    (void)Src0Var;
    assert(Src0Var->getRegNum() == RegisterSet::Reg_al);
    static const GPREmitterOneOp Emitter = {&Assembler::imul, &Assembler::imul};
    emitIASOpTyGPR(Func, Ty, this->getSrc(1), Emitter);
  } else {
    // The two-address version is used when multiplying by a non-constant
    // or doing an 8-bit multiply.
    assert(Var == this->getSrc(0));
    static const GPREmitterRegOp Emitter = {&Assembler::imul, &Assembler::imul,
                                            &Assembler::imul};
    constexpr bool NotLea = false;
    emitIASRegOpTyGPR(Func, NotLea, Ty, Var, Src, Emitter);
  }
}

void InstX86ImulImm::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 2);
  Variable *Dest = this->getDest();
  assert(Dest->getType() == IceType_i16 || Dest->getType() == IceType_i32);
  assert(llvm::isa<Constant>(this->getSrc(1)));
  Str << "\t"
         "imul"
      << this->getWidthString(Dest->getType()) << "\t";
  this->getSrc(1)->emit(Func);
  Str << ", ";
  this->getSrc(0)->emit(Func);
  Str << ", ";
  Dest->emit(Func);
}

void InstX86ImulImm::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 2);
  const Variable *Dest = this->getDest();
  Type Ty = Dest->getType();
  assert(llvm::isa<Constant>(this->getSrc(1)));
  static const ThreeOpImmEmitter<GPRRegister, GPRRegister> Emitter = {
      &Assembler::imul, &Assembler::imul};
  emitIASThreeOpImmOps<GPRRegister, GPRRegister, Traits::getEncodedGPR,
                       Traits::getEncodedGPR>(Func, Ty, Dest, this->getSrc(0),
                                              this->getSrc(1), Emitter);
}

void InstX86Insertps::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 3);
  assert(InstX86Base::getTarget(Func)->getInstructionSet() >= SSE4_1);
  const Variable *Dest = this->getDest();
  assert(Dest == this->getSrc(0));
  Type Ty = Dest->getType();
  static const ThreeOpImmEmitter<XmmRegister, XmmRegister> Emitter = {
      &Assembler::insertps, &Assembler::insertps};
  emitIASThreeOpImmOps<XmmRegister, XmmRegister, Traits::getEncodedXmm,
                       Traits::getEncodedXmm>(Func, Ty, Dest, this->getSrc(1),
                                              this->getSrc(2), Emitter);
}

void InstX86Cbwdq::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 1);
  Operand *Src0 = this->getSrc(0);
  const auto DestReg = this->getDest()->getRegNum();
  const auto SrcReg = llvm::cast<Variable>(Src0)->getRegNum();
  (void)DestReg;
  (void)SrcReg;
  switch (Src0->getType()) {
  default:
    llvm_unreachable("unexpected source type!");
    break;
  case IceType_i8:
    assert(SrcReg == RegisterSet::Reg_al);
    assert(DestReg == RegisterSet::Reg_ax || DestReg == RegisterSet::Reg_ah);
    Str << "\t"
           "cbtw";
    break;
  case IceType_i16:
    assert(SrcReg == RegisterSet::Reg_ax);
    assert(DestReg == RegisterSet::Reg_dx);
    Str << "\t"
           "cwtd";
    break;
  case IceType_i32:
    assert(SrcReg == RegisterSet::Reg_eax);
    assert(DestReg == RegisterSet::Reg_edx);
    Str << "\t"
           "cltd";
    break;
  case IceType_i64:
    assert(Traits::Is64Bit);
    assert(SrcReg == Traits::getRaxOrDie());
    assert(DestReg == Traits::getRdxOrDie());
    Str << "\t"
           "cqo";
    break;
  }
}

void InstX86Cbwdq::emitIAS(const Cfg *Func) const {
  Assembler *Asm = Func->getAssembler<Assembler>();
  assert(this->getSrcSize() == 1);
  Operand *Src0 = this->getSrc(0);
  const auto DestReg = this->getDest()->getRegNum();
  const auto SrcReg = llvm::cast<Variable>(Src0)->getRegNum();
  (void)DestReg;
  (void)SrcReg;
  switch (Src0->getType()) {
  default:
    llvm_unreachable("unexpected source type!");
    break;
  case IceType_i8:
    assert(SrcReg == RegisterSet::Reg_al);
    assert(DestReg == RegisterSet::Reg_ax || DestReg == RegisterSet::Reg_ah);
    Asm->cbw();
    break;
  case IceType_i16:
    assert(SrcReg == RegisterSet::Reg_ax);
    assert(DestReg == RegisterSet::Reg_dx);
    Asm->cwd();
    break;
  case IceType_i32:
    assert(SrcReg == RegisterSet::Reg_eax);
    assert(DestReg == RegisterSet::Reg_edx);
    Asm->cdq();
    break;
  case IceType_i64:
    assert(Traits::Is64Bit);
    assert(SrcReg == Traits::getRaxOrDie());
    assert(DestReg == Traits::getRdxOrDie());
    Asm->cqo();
    break;
  }
}

void InstX86Mul::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 2);
  assert(llvm::isa<Variable>(this->getSrc(0)));
  assert(llvm::cast<Variable>(this->getSrc(0))->getRegNum() ==
         RegisterSet::Reg_eax);
  assert(this->getDest()->getRegNum() == RegisterSet::Reg_eax); // TODO:
                                                                // allow
                                                                // edx?
  Str << "\t"
         "mul"
      << this->getWidthString(this->getDest()->getType()) << "\t";
  this->getSrc(1)->emit(Func);
}

void InstX86Mul::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 2);
  assert(llvm::isa<Variable>(this->getSrc(0)));
  assert(llvm::cast<Variable>(this->getSrc(0))->getRegNum() ==
         RegisterSet::Reg_eax);
  assert(this->getDest()->getRegNum() == RegisterSet::Reg_eax); // TODO:
                                                                // allow
                                                                // edx?
  const Operand *Src = this->getSrc(1);
  Type Ty = Src->getType();
  static const GPREmitterOneOp Emitter = {&Assembler::mul, &Assembler::mul};
  emitIASOpTyGPR(Func, Ty, Src, Emitter);
}

void InstX86Mul::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  this->dumpDest(Func);
  Str << " = mul." << this->getDest()->getType() << " ";
  this->dumpSources(Func);
}

void InstX86Shld::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  Variable *Dest = this->getDest();
  assert(this->getSrcSize() == 3);
  assert(Dest == this->getSrc(0));
  Str << "\t"
         "shld"
      << this->getWidthString(Dest->getType()) << "\t";
  this->getSrc(2)->emit(Func);
  Str << ", ";
  this->getSrc(1)->emit(Func);
  Str << ", ";
  Dest->emit(Func);
}

void InstX86Shld::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 3);
  assert(this->getDest() == this->getSrc(0));
  const Variable *Dest = this->getDest();
  const Operand *Src1 = this->getSrc(1);
  const Operand *Src2 = this->getSrc(2);
  static const GPREmitterShiftD Emitter = {&Assembler::shld, &Assembler::shld};
  emitIASGPRShiftDouble(Func, Dest, Src1, Src2, Emitter);
}

void InstX86Shld::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  this->dumpDest(Func);
  Str << " = shld." << this->getDest()->getType() << " ";
  this->dumpSources(Func);
}

void InstX86Shrd::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  Variable *Dest = this->getDest();
  assert(this->getSrcSize() == 3);
  assert(Dest == this->getSrc(0));
  Str << "\t"
         "shrd"
      << this->getWidthString(Dest->getType()) << "\t";
  this->getSrc(2)->emit(Func);
  Str << ", ";
  this->getSrc(1)->emit(Func);
  Str << ", ";
  Dest->emit(Func);
}

void InstX86Shrd::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 3);
  assert(this->getDest() == this->getSrc(0));
  const Variable *Dest = this->getDest();
  const Operand *Src1 = this->getSrc(1);
  const Operand *Src2 = this->getSrc(2);
  static const GPREmitterShiftD Emitter = {&Assembler::shrd, &Assembler::shrd};
  emitIASGPRShiftDouble(Func, Dest, Src1, Src2, Emitter);
}

void InstX86Shrd::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  this->dumpDest(Func);
  Str << " = shrd." << this->getDest()->getType() << " ";
  this->dumpSources(Func);
}

void InstX86Cmov::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  Variable *Dest = this->getDest();
  Str << "\t";
  assert(Condition != Cond::Br_None);
  assert(this->getDest()->hasReg());
  Str << "cmov" << Traits::InstBrAttributes[Condition].DisplayString
      << this->getWidthString(Dest->getType()) << "\t";
  this->getSrc(1)->emit(Func);
  Str << ", ";
  Dest->emit(Func);
}

void InstX86Cmov::emitIAS(const Cfg *Func) const {
  assert(Condition != Cond::Br_None);
  assert(this->getDest()->hasReg());
  assert(this->getSrcSize() == 2);
  Operand *Src = this->getSrc(1);
  Type SrcTy = Src->getType();
  assert(SrcTy == IceType_i16 || SrcTy == IceType_i32 || (Traits::Is64Bit));
  Assembler *Asm = Func->getAssembler<Assembler>();
  auto *Target = InstX86Base::getTarget(Func);
  if (const auto *SrcVar = llvm::dyn_cast<Variable>(Src)) {
    if (SrcVar->hasReg()) {
      Asm->cmov(SrcTy, Condition,
                Traits::getEncodedGPR(this->getDest()->getRegNum()),
                Traits::getEncodedGPR(SrcVar->getRegNum()));
    } else {
      Asm->cmov(SrcTy, Condition,
                Traits::getEncodedGPR(this->getDest()->getRegNum()),
                Target->stackVarToAsmOperand(SrcVar));
    }
  } else if (const auto *Mem = llvm::dyn_cast<X86OperandMem>(Src)) {
    assert(Mem->getSegmentRegister() == X86OperandMem::DefaultSegment);
    Asm->cmov(SrcTy, Condition,
              Traits::getEncodedGPR(this->getDest()->getRegNum()),
              Mem->toAsmAddress(Asm, Target));
  } else {
    llvm_unreachable("Unexpected operand type");
  }
}

void InstX86Cmov::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "cmov" << Traits::InstBrAttributes[Condition].DisplayString << ".";
  Str << this->getDest()->getType() << " ";
  this->dumpDest(Func);
  Str << ", ";
  this->dumpSources(Func);
}

void InstX86Cmpps::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 2);
  assert(Condition < Cond::Cmpps_Invalid);
  Type DestTy = this->Dest->getType();
  Str << "\t"
         "cmp"
      << Traits::InstCmppsAttributes[Condition].EmitString
      << Traits::TypeAttributes[DestTy].PdPsString << "\t";
  this->getSrc(1)->emit(Func);
  Str << ", ";
  this->getDest()->emit(Func);
}

void InstX86Cmpps::emitIAS(const Cfg *Func) const {
  Assembler *Asm = Func->getAssembler<Assembler>();
  assert(this->getSrcSize() == 2);
  assert(Condition < Cond::Cmpps_Invalid);
  // Assuming there isn't any load folding for cmpps, and vector constants are
  // not allowed in PNaCl.
  assert(llvm::isa<Variable>(this->getSrc(1)));
  auto *Target = InstX86Base::getTarget(Func);
  const auto *SrcVar = llvm::cast<Variable>(this->getSrc(1));
  if (SrcVar->hasReg()) {
    Asm->cmpps(this->getDest()->getType(),
               Traits::getEncodedXmm(this->getDest()->getRegNum()),
               Traits::getEncodedXmm(SrcVar->getRegNum()), Condition);
  } else {
    Address SrcStackAddr = Target->stackVarToAsmOperand(SrcVar);
    Asm->cmpps(this->getDest()->getType(),
               Traits::getEncodedXmm(this->getDest()->getRegNum()),
               SrcStackAddr, Condition);
  }
}

void InstX86Cmpps::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  assert(Condition < Cond::Cmpps_Invalid);
  this->dumpDest(Func);
  Str << " = cmp" << Traits::InstCmppsAttributes[Condition].EmitString
      << "ps"
         "\t";
  this->dumpSources(Func);
}

void InstX86Cmpxchg::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 3);
  if (this->Locked) {
    Str << "\t"
           "lock";
  }
  Str << "\t"
         "cmpxchg"
      << this->getWidthString(this->getSrc(0)->getType()) << "\t";
  this->getSrc(2)->emit(Func);
  Str << ", ";
  this->getSrc(0)->emit(Func);
}

void InstX86Cmpxchg::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 3);
  Assembler *Asm = Func->getAssembler<Assembler>();
  Type Ty = this->getSrc(0)->getType();
  auto *Target = InstX86Base::getTarget(Func);
  const auto Mem = llvm::cast<X86OperandMem>(this->getSrc(0));
  assert(Mem->getSegmentRegister() == X86OperandMem::DefaultSegment);
  const Address Addr = Mem->toAsmAddress(Asm, Target);
  const auto *VarReg = llvm::cast<Variable>(this->getSrc(2));
  assert(VarReg->hasReg());
  const GPRRegister Reg = Traits::getEncodedGPR(VarReg->getRegNum());
  Asm->cmpxchg(Ty, Addr, Reg, this->Locked);
}

void InstX86Cmpxchg::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  if (this->Locked) {
    Str << "lock ";
  }
  Str << "cmpxchg." << this->getSrc(0)->getType() << " ";
  this->dumpSources(Func);
}

void InstX86Cmpxchg8b::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 5);
  if (this->Locked) {
    Str << "\t"
           "lock";
  }
  Str << "\t"
         "cmpxchg8b\t";
  this->getSrc(0)->emit(Func);
}

void InstX86Cmpxchg8b::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 5);
  Assembler *Asm = Func->getAssembler<Assembler>();
  const auto Mem = llvm::cast<X86OperandMem>(this->getSrc(0));
  assert(Mem->getSegmentRegister() == X86OperandMem::DefaultSegment);
  auto *Target = InstX86Base::getTarget(Func);
  const Address Addr = Mem->toAsmAddress(Asm, Target);
  Asm->cmpxchg8b(Addr, this->Locked);
}

void InstX86Cmpxchg8b::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  if (this->Locked) {
    Str << "lock ";
  }
  Str << "cmpxchg8b ";
  this->dumpSources(Func);
}

void InstX86Cvt::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 1);
  Str << "\t"
         "cvt";
  if (isTruncating())
    Str << "t";
  Str << Traits::TypeAttributes[this->getSrc(0)->getType()].CvtString << "2"
      << Traits::TypeAttributes[this->getDest()->getType()].CvtString << "\t";
  this->getSrc(0)->emit(Func);
  Str << ", ";
  this->getDest()->emit(Func);
}

void InstX86Cvt::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 1);
  const Variable *Dest = this->getDest();
  const Operand *Src = this->getSrc(0);
  Type DestTy = Dest->getType();
  Type SrcTy = Src->getType();
  switch (Variant) {
  case Si2ss: {
    assert(isScalarIntegerType(SrcTy));
    if (!Traits::Is64Bit) {
      assert(typeWidthInBytes(SrcTy) <= 4);
    } else {
      assert(SrcTy == IceType_i32 || SrcTy == IceType_i64);
    }
    assert(isScalarFloatingType(DestTy));
    static const CastEmitterRegOp<XmmRegister, GPRRegister> Emitter = {
        &Assembler::cvtsi2ss, &Assembler::cvtsi2ss};
    emitIASCastRegOp<XmmRegister, GPRRegister, Traits::getEncodedXmm,
                     Traits::getEncodedGPR>(Func, DestTy, Dest, SrcTy, Src,
                                            Emitter);
    return;
  }
  case Tss2si: {
    assert(isScalarFloatingType(SrcTy));
    assert(isScalarIntegerType(DestTy));
    if (Traits::Is64Bit) {
      assert(DestTy == IceType_i32 || DestTy == IceType_i64);
    } else {
      assert(typeWidthInBytes(DestTy) <= 4);
    }
    static const CastEmitterRegOp<GPRRegister, XmmRegister> Emitter = {
        &Assembler::cvttss2si, &Assembler::cvttss2si};
    emitIASCastRegOp<GPRRegister, XmmRegister, Traits::getEncodedGPR,
                     Traits::getEncodedXmm>(Func, DestTy, Dest, SrcTy, Src,
                                            Emitter);
    return;
  }
  case Ss2si: {
    assert(isScalarFloatingType(SrcTy));
    assert(isScalarIntegerType(DestTy));
    if (Traits::Is64Bit) {
      assert(DestTy == IceType_i32 || DestTy == IceType_i64);
    } else {
      assert(typeWidthInBytes(DestTy) <= 4);
    }
    static const CastEmitterRegOp<GPRRegister, XmmRegister> Emitter = {
        &Assembler::cvtss2si, &Assembler::cvtss2si};
    emitIASCastRegOp<GPRRegister, XmmRegister, Traits::getEncodedGPR,
                     Traits::getEncodedXmm>(Func, DestTy, Dest, SrcTy, Src,
                                            Emitter);
    return;
  }
  case Float2float: {
    assert(isScalarFloatingType(SrcTy));
    assert(isScalarFloatingType(DestTy));
    assert(DestTy != SrcTy);
    static const XmmEmitterRegOp Emitter = {&Assembler::cvtfloat2float,
                                            &Assembler::cvtfloat2float};
    emitIASRegOpTyXMM(Func, SrcTy, Dest, Src, Emitter);
    return;
  }
  case Dq2ps: {
    assert(isVectorIntegerType(SrcTy));
    assert(isVectorFloatingType(DestTy));
    static const XmmEmitterRegOp Emitter = {&Assembler::cvtdq2ps,
                                            &Assembler::cvtdq2ps};
    emitIASRegOpTyXMM(Func, DestTy, Dest, Src, Emitter);
    return;
  }
  case Tps2dq: {
    assert(isVectorFloatingType(SrcTy));
    assert(isVectorIntegerType(DestTy));
    static const XmmEmitterRegOp Emitter = {&Assembler::cvttps2dq,
                                            &Assembler::cvttps2dq};
    emitIASRegOpTyXMM(Func, DestTy, Dest, Src, Emitter);
    return;
  }
  case Ps2dq: {
    assert(isVectorFloatingType(SrcTy));
    assert(isVectorIntegerType(DestTy));
    static const XmmEmitterRegOp Emitter = {&Assembler::cvtps2dq,
                                            &Assembler::cvtps2dq};
    emitIASRegOpTyXMM(Func, DestTy, Dest, Src, Emitter);
    return;
  }
  }
}

void InstX86Cvt::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  this->dumpDest(Func);
  Str << " = cvt";
  if (isTruncating())
    Str << "t";
  Str << Traits::TypeAttributes[this->getSrc(0)->getType()].CvtString << "2"
      << Traits::TypeAttributes[this->getDest()->getType()].CvtString << " ";
  this->dumpSources(Func);
}

void InstX86Round::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 3);
  Str << "\t" << this->Opcode
      << Traits::TypeAttributes[this->getDest()->getType()].SpSdString << "\t";
  this->getSrc(1)->emit(Func);
  Str << ", ";
  this->getSrc(0)->emit(Func);
  Str << ", ";
  this->getDest()->emit(Func);
}

void InstX86Round::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 2);
  assert(InstX86Base::getTarget(Func)->getInstructionSet() >= SSE4_1);
  const Variable *Dest = this->getDest();
  Type Ty = Dest->getType();
  static const ThreeOpImmEmitter<XmmRegister, XmmRegister> Emitter = {
      &Assembler::round, &Assembler::round};
  emitIASThreeOpImmOps<XmmRegister, XmmRegister, Traits::getEncodedXmm,
                       Traits::getEncodedXmm>(Func, Ty, Dest, this->getSrc(0),
                                              this->getSrc(1), Emitter);
}

void InstX86Icmp::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 2);
  Str << "\t"
         "cmp"
      << this->getWidthString(this->getSrc(0)->getType()) << "\t";
  this->getSrc(1)->emit(Func);
  Str << ", ";
  this->getSrc(0)->emit(Func);
}

void InstX86Icmp::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 2);
  const Operand *Src0 = this->getSrc(0);
  const Operand *Src1 = this->getSrc(1);
  Type Ty = Src0->getType();
  static const GPREmitterRegOp RegEmitter = {&Assembler::cmp, &Assembler::cmp,
                                             &Assembler::cmp};
  static const GPREmitterAddrOp AddrEmitter = {&Assembler::cmp,
                                               &Assembler::cmp};
  if (const auto *SrcVar0 = llvm::dyn_cast<Variable>(Src0)) {
    if (SrcVar0->hasReg()) {
      constexpr bool NotLea = false;
      emitIASRegOpTyGPR(Func, NotLea, Ty, SrcVar0, Src1, RegEmitter);
      return;
    }
  }
  emitIASAsAddrOpTyGPR(Func, Ty, Src0, Src1, AddrEmitter);
}

void InstX86Icmp::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "cmp." << this->getSrc(0)->getType() << " ";
  this->dumpSources(Func);
}

void InstX86Ucomiss::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 2);
  Str << "\t"
         "ucomi"
      << Traits::TypeAttributes[this->getSrc(0)->getType()].SdSsString << "\t";
  this->getSrc(1)->emit(Func);
  Str << ", ";
  this->getSrc(0)->emit(Func);
}

void InstX86Ucomiss::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 2);
  // Currently src0 is always a variable by convention, to avoid having two
  // memory operands.
  assert(llvm::isa<Variable>(this->getSrc(0)));
  const auto *Src0Var = llvm::cast<Variable>(this->getSrc(0));
  Type Ty = Src0Var->getType();
  static const XmmEmitterRegOp Emitter = {&Assembler::ucomiss,
                                          &Assembler::ucomiss};
  emitIASRegOpTyXMM(Func, Ty, Src0Var, this->getSrc(1), Emitter);
}

void InstX86Ucomiss::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "ucomiss." << this->getSrc(0)->getType() << " ";
  this->dumpSources(Func);
}

void InstX86UD2::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 0);
  Str << "\t"
         "ud2";
}

void InstX86UD2::emitIAS(const Cfg *Func) const {
  Assembler *Asm = Func->getAssembler<Assembler>();
  Asm->ud2();
}

void InstX86UD2::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "ud2";
}

void InstX86Int3::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 0);
  Str << "\t"
         "int 3";
}

void InstX86Int3::emitIAS(const Cfg *Func) const {
  Assembler *Asm = Func->getAssembler<Assembler>();
  Asm->int3();
}

void InstX86Int3::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "int 3";
}

void InstX86Test::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 2);
  Str << "\t"
         "test"
      << this->getWidthString(this->getSrc(0)->getType()) << "\t";
  this->getSrc(1)->emit(Func);
  Str << ", ";
  this->getSrc(0)->emit(Func);
}

void InstX86Test::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 2);
  const Operand *Src0 = this->getSrc(0);
  const Operand *Src1 = this->getSrc(1);
  Type Ty = Src0->getType();
  // The Reg/Addr form of test is not encodeable.
  static const GPREmitterRegOp RegEmitter = {&Assembler::test, nullptr,
                                             &Assembler::test};
  static const GPREmitterAddrOp AddrEmitter = {&Assembler::test,
                                               &Assembler::test};
  if (const auto *SrcVar0 = llvm::dyn_cast<Variable>(Src0)) {
    if (SrcVar0->hasReg()) {
      constexpr bool NotLea = false;
      emitIASRegOpTyGPR(Func, NotLea, Ty, SrcVar0, Src1, RegEmitter);
      return;
    }
  }
  emitIASAsAddrOpTyGPR(Func, Ty, Src0, Src1, AddrEmitter);
}

void InstX86Test::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "test." << this->getSrc(0)->getType() << " ";
  this->dumpSources(Func);
}

void InstX86Mfence::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 0);
  Str << "\t"
         "mfence";
}

void InstX86Mfence::emitIAS(const Cfg *Func) const {
  Assembler *Asm = Func->getAssembler<Assembler>();
  Asm->mfence();
}

void InstX86Mfence::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "mfence";
}

void InstX86Store::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 2);
  Type Ty = this->getSrc(0)->getType();
  Str << "\t"
         "mov"
      << this->getWidthString(Ty) << Traits::TypeAttributes[Ty].SdSsString
      << "\t";
  this->getSrc(0)->emit(Func);
  Str << ", ";
  this->getSrc(1)->emit(Func);
}

void InstX86Store::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 2);
  const Operand *Dest = this->getSrc(1);
  const Operand *Src = this->getSrc(0);
  Type DestTy = Dest->getType();
  if (isScalarFloatingType(DestTy)) {
    // Src must be a register, since Dest is a Mem operand of some kind.
    const auto *SrcVar = llvm::cast<Variable>(Src);
    assert(SrcVar->hasReg());
    XmmRegister SrcReg = Traits::getEncodedXmm(SrcVar->getRegNum());
    Assembler *Asm = Func->getAssembler<Assembler>();
    auto *Target = InstX86Base::getTarget(Func);
    if (const auto *DestVar = llvm::dyn_cast<Variable>(Dest)) {
      assert(!DestVar->hasReg());
      Address StackAddr(Target->stackVarToAsmOperand(DestVar));
      Asm->movss(DestTy, StackAddr, SrcReg);
    } else {
      const auto DestMem = llvm::cast<X86OperandMem>(Dest);
      assert(DestMem->getSegmentRegister() == X86OperandMem::DefaultSegment);
      Asm->movss(DestTy, DestMem->toAsmAddress(Asm, Target), SrcReg);
    }
    return;
  } else {
    assert(isScalarIntegerType(DestTy));
    static const GPREmitterAddrOp GPRAddrEmitter = {&Assembler::mov,
                                                    &Assembler::mov};
    emitIASAsAddrOpTyGPR(Func, DestTy, Dest, Src, GPRAddrEmitter);
  }
}

void InstX86Store::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "mov." << this->getSrc(0)->getType() << " ";
  this->getSrc(1)->dump(Func);
  Str << ", ";
  this->getSrc(0)->dump(Func);
}

void InstX86StoreP::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 2);
  assert(isVectorType(this->getSrc(1)->getType()));
  Str << "\t"
         "movups\t";
  this->getSrc(0)->emit(Func);
  Str << ", ";
  this->getSrc(1)->emit(Func);
}

void InstX86StoreP::emitIAS(const Cfg *Func) const {
  Assembler *Asm = Func->getAssembler<Assembler>();
  assert(this->getSrcSize() == 2);
  const auto *SrcVar = llvm::cast<Variable>(this->getSrc(0));
  const auto DestMem = llvm::cast<X86OperandMem>(this->getSrc(1));
  assert(DestMem->getSegmentRegister() == X86OperandMem::DefaultSegment);
  assert(SrcVar->hasReg());
  auto *Target = InstX86Base::getTarget(Func);
  Asm->movups(DestMem->toAsmAddress(Asm, Target),
              Traits::getEncodedXmm(SrcVar->getRegNum()));
}

void InstX86StoreP::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "storep." << this->getSrc(0)->getType() << " ";
  this->getSrc(1)->dump(Func);
  Str << ", ";
  this->getSrc(0)->dump(Func);
}

void InstX86StoreQ::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 2);
  assert(this->getSrc(1)->getType() == IceType_i64 ||
         this->getSrc(1)->getType() == IceType_f64 ||
         isVectorType(this->getSrc(1)->getType()));
  Str << "\t"
         "movq\t";
  this->getSrc(0)->emit(Func);
  Str << ", ";
  this->getSrc(1)->emit(Func);
}

void InstX86StoreQ::emitIAS(const Cfg *Func) const {
  Assembler *Asm = Func->getAssembler<Assembler>();
  assert(this->getSrcSize() == 2);
  const auto *SrcVar = llvm::cast<Variable>(this->getSrc(0));
  const auto DestMem = llvm::cast<X86OperandMem>(this->getSrc(1));
  assert(DestMem->getSegmentRegister() == X86OperandMem::DefaultSegment);
  assert(SrcVar->hasReg());
  auto *Target = InstX86Base::getTarget(Func);
  Asm->movq(DestMem->toAsmAddress(Asm, Target),
            Traits::getEncodedXmm(SrcVar->getRegNum()));
}

void InstX86StoreQ::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "storeq." << this->getSrc(0)->getType() << " ";
  this->getSrc(1)->dump(Func);
  Str << ", ";
  this->getSrc(0)->dump(Func);
}

void InstX86StoreD::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 2);
  assert(this->getSrc(1)->getType() == IceType_i64 ||
         this->getSrc(1)->getType() == IceType_f64 ||
         isVectorType(this->getSrc(1)->getType()));
  Str << "\t"
         "movd\t";
  this->getSrc(0)->emit(Func);
  Str << ", ";
  this->getSrc(1)->emit(Func);
}

void InstX86StoreD::emitIAS(const Cfg *Func) const {
  Assembler *Asm = Func->getAssembler<Assembler>();
  assert(this->getSrcSize() == 2);
  const auto *SrcVar = llvm::cast<Variable>(this->getSrc(0));
  const auto DestMem = llvm::cast<X86OperandMem>(this->getSrc(1));
  assert(DestMem->getSegmentRegister() == X86OperandMem::DefaultSegment);
  assert(SrcVar->hasReg());
  auto *Target = InstX86Base::getTarget(Func);
  Asm->movd(SrcVar->getType(), DestMem->toAsmAddress(Asm, Target),
            Traits::getEncodedXmm(SrcVar->getRegNum()));
}

void InstX86StoreD::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "stored." << this->getSrc(0)->getType() << " ";
  this->getSrc(1)->dump(Func);
  Str << ", ";
  this->getSrc(0)->dump(Func);
}

void InstX86Lea::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  if (auto *Add = this->deoptToAddOrNull(Func)) {
    Add->emit(Func);
    return;
  }

  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 1);
  assert(this->getDest()->hasReg());
  Str << "\t"
         "lea"
      << this->getWidthString(this->getDest()->getType()) << "\t";
  Operand *Src0 = this->getSrc(0);
  if (const auto *Src0Var = llvm::dyn_cast<Variable>(Src0)) {
    Type Ty = Src0Var->getType();
    // lea on x86-32 doesn't accept mem128 operands, so cast VSrc0 to an
    // acceptable type.
    Src0Var->asType(Func, isVectorType(Ty) ? IceType_i32 : Ty, RegNumT())
        ->emit(Func);
  } else {
    Src0->emit(Func);
  }
  Str << ", ";
  this->getDest()->emit(Func);
}

void InstX86Lea::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 1);
  const Variable *Var = this->getDest();
  Type Ty = Var->getType();
  const Operand *Src = this->getSrc(0);
  bool IsLea = true;

  if (auto *Add = this->deoptToAddOrNull(Func)) {
    Add->emitIAS(Func);
    return;
  }

  emitIASRegOpTyGPR(Func, IsLea, Ty, Var, Src, Emitter);
}

Inst *InstX86Lea::deoptToAddOrNull(const Cfg *Func) const {
  // Revert back to Add when the Lea is a 2-address instruction.
  // Caller has to emit, this just produces the add instruction.
  if (auto *MemOp = llvm::dyn_cast<X86OperandMem>(this->getSrc(0))) {
    if (getFlags().getAggressiveLea() &&
        MemOp->getBase()->getRegNum() == this->getDest()->getRegNum() &&
        MemOp->getIndex() == nullptr && MemOp->getShift() == 0) {
      auto *Add = InstX86Add::create(const_cast<Cfg *>(Func), this->getDest(),
                                     MemOp->getOffset());
      // TODO(manasijm): Remove const_cast by emitting code for add
      // directly.
      return Add;
    }
  }
  return nullptr;
}

void InstX86Mov::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 1);
  Operand *Src = this->getSrc(0);
  Type SrcTy = Src->getType();
  Type DestTy = this->getDest()->getType();
  if (Traits::Is64Bit && DestTy == IceType_i64 &&
      llvm::isa<ConstantInteger64>(Src) &&
      !Utils::IsInt(32, llvm::cast<ConstantInteger64>(Src)->getValue())) {
    Str << "\t"
           "movabs"
           "\t";
  } else {
    Str << "\t"
           "mov"
        << (!isScalarFloatingType(DestTy)
                ? this->getWidthString(DestTy)
                : Traits::TypeAttributes[DestTy].SdSsString)
        << "\t";
  }
  // For an integer truncation operation, src is wider than dest. In this case,
  // we use a mov instruction whose data width matches the narrower dest.
  // TODO: This assert disallows usages such as copying a floating
  // point value between a vector and a scalar (which movss is used for). Clean
  // this up.
  assert(InstX86Base::getTarget(Func)->typeWidthInBytesOnStack(DestTy) ==
         InstX86Base::getTarget(Func)->typeWidthInBytesOnStack(SrcTy));
  const Operand *NewSrc = Src;
  if (auto *SrcVar = llvm::dyn_cast<Variable>(Src)) {
    RegNumT NewRegNum;
    if (SrcVar->hasReg())
      NewRegNum = Traits::getGprForType(DestTy, SrcVar->getRegNum());
    if (SrcTy != DestTy)
      NewSrc = SrcVar->asType(Func, DestTy, NewRegNum);
  }
  NewSrc->emit(Func);
  Str << ", ";
  this->getDest()->emit(Func);
}

void InstX86Mov::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 1);
  const Variable *Dest = this->getDest();
  const Operand *Src = this->getSrc(0);
  Type DestTy = Dest->getType();
  Type SrcTy = Src->getType();
  // Mov can be used for GPRs or XMM registers. Also, the type does not
  // necessarily match (Mov can be used for bitcasts). However, when the type
  // does not match, one of the operands must be a register. Thus, the strategy
  // is to find out if Src or Dest are a register, then use that register's
  // type to decide on which emitter set to use. The emitter set will include
  // reg-reg movs, but that case should be unused when the types don't match.
  static const XmmEmitterRegOp XmmRegEmitter = {&Assembler::movss,
                                                &Assembler::movss};
  static const GPREmitterRegOp GPRRegEmitter = {
      &Assembler::mov, &Assembler::mov, &Assembler::mov};
  static const GPREmitterAddrOp GPRAddrEmitter = {&Assembler::mov,
                                                  &Assembler::mov};
  // For an integer truncation operation, src is wider than dest. In this case,
  // we use a mov instruction whose data width matches the narrower dest.
  // TODO: This assert disallows usages such as copying a floating
  // point value between a vector and a scalar (which movss is used for). Clean
  // this up.
  auto *Target = InstX86Base::getTarget(Func);
  assert(Target->typeWidthInBytesOnStack(this->getDest()->getType()) ==
         Target->typeWidthInBytesOnStack(Src->getType()));
  if (Dest->hasReg()) {
    if (isScalarFloatingType(DestTy)) {
      emitIASRegOpTyXMM(Func, DestTy, Dest, Src, XmmRegEmitter);
      return;
    } else {
      assert(isScalarIntegerType(DestTy));
      // Widen DestTy for truncation (see above note). We should only do this
      // when both Src and Dest are integer types.
      if (isScalarIntegerType(SrcTy)) {
        SrcTy = DestTy;
      }
      constexpr bool NotLea = false;
      emitIASRegOpTyGPR(Func, NotLea, DestTy, Dest, Src, GPRRegEmitter);
      return;
    }
  } else {
    // Dest must be Stack and Src *could* be a register. Use Src's type to
    // decide on the emitters.
    Address StackAddr(Target->stackVarToAsmOperand(Dest));
    if (isScalarFloatingType(SrcTy)) {
      // Src must be a register.
      const auto *SrcVar = llvm::cast<Variable>(Src);
      assert(SrcVar->hasReg());
      Assembler *Asm = Func->getAssembler<Assembler>();
      Asm->movss(SrcTy, StackAddr, Traits::getEncodedXmm(SrcVar->getRegNum()));
      return;
    } else if (isVectorType(SrcTy)) {
      // Src must be a register
      const auto *SrcVar = llvm::cast<Variable>(Src);
      assert(SrcVar->hasReg());
      Assembler *Asm = Func->getAssembler<Assembler>();
      Asm->movups(StackAddr, Traits::getEncodedXmm(SrcVar->getRegNum()));
    } else {
      // Src can be a register or immediate.
      assert(isScalarIntegerType(SrcTy));
      emitIASAddrOpTyGPR(Func, SrcTy, StackAddr, Src, GPRAddrEmitter);
      return;
    }
    return;
  }
}

void InstX86Movd::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  assert(this->getSrcSize() == 1);
  Variable *Dest = this->getDest();
  Operand *Src = this->getSrc(0);

  if (Dest->getType() == IceType_i64 || Src->getType() == IceType_i64) {
    assert(Dest->getType() == IceType_f64 || Src->getType() == IceType_f64);
    assert(Dest->getType() != Src->getType());
    Ostream &Str = Func->getContext()->getStrEmit();
    Str << "\t"
           "movq"
           "\t";
    Src->emit(Func);
    Str << ", ";
    Dest->emit(Func);
    return;
  }

  InstX86BaseUnaryopXmm<InstX86Base::Movd>::emit(Func);
}

void InstX86Movd::emitIAS(const Cfg *Func) const {
  Assembler *Asm = Func->getAssembler<Assembler>();
  assert(this->getSrcSize() == 1);
  const Variable *Dest = this->getDest();
  auto *Target = InstX86Base::getTarget(Func);
  // For insert/extract element (one of Src/Dest is an Xmm vector and the other
  // is an int type).
  if (const auto *SrcVar = llvm::dyn_cast<Variable>(this->getSrc(0))) {
    if (SrcVar->getType() == IceType_i32 ||
        (Traits::Is64Bit && SrcVar->getType() == IceType_i64)) {
      assert(isVectorType(Dest->getType()) ||
             (isScalarFloatingType(Dest->getType()) &&
              typeWidthInBytes(SrcVar->getType()) ==
                  typeWidthInBytes(Dest->getType())));
      assert(Dest->hasReg());
      XmmRegister DestReg = Traits::getEncodedXmm(Dest->getRegNum());
      if (SrcVar->hasReg()) {
        Asm->movd(SrcVar->getType(), DestReg,
                  Traits::getEncodedGPR(SrcVar->getRegNum()));
      } else {
        Address StackAddr(Target->stackVarToAsmOperand(SrcVar));
        Asm->movd(SrcVar->getType(), DestReg, StackAddr);
      }
    } else {
      assert(isVectorType(SrcVar->getType()) ||
             (isScalarFloatingType(SrcVar->getType()) &&
              typeWidthInBytes(SrcVar->getType()) ==
                  typeWidthInBytes(Dest->getType())));
      assert(SrcVar->hasReg());
      assert(Dest->getType() == IceType_i32 ||
             (Traits::Is64Bit && Dest->getType() == IceType_i64));
      XmmRegister SrcReg = Traits::getEncodedXmm(SrcVar->getRegNum());
      if (Dest->hasReg()) {
        Asm->movd(Dest->getType(), Traits::getEncodedGPR(Dest->getRegNum()),
                  SrcReg);
      } else {
        Address StackAddr(Target->stackVarToAsmOperand(Dest));
        Asm->movd(Dest->getType(), StackAddr, SrcReg);
      }
    }
  } else {
    assert(Dest->hasReg());
    XmmRegister DestReg = Traits::getEncodedXmm(Dest->getRegNum());
    auto *Mem = llvm::cast<X86OperandMem>(this->getSrc(0));
    Asm->movd(Mem->getType(), DestReg, Mem->toAsmAddress(Asm, Target));
  }
}

void InstX86Movp::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  // TODO(wala,stichnot): movups works with all vector operands, but there
  // exist other instructions (movaps, movdqa, movdqu) that may perform better,
  // depending on the data type and alignment of the operands.
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 1);
  Str << "\t"
         "movups\t";
  this->getSrc(0)->emit(Func);
  Str << ", ";
  this->getDest()->emit(Func);
}

void InstX86Movp::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 1);
  assert(isVectorType(this->getDest()->getType()));
  const Variable *Dest = this->getDest();
  const Operand *Src = this->getSrc(0);
  static const XmmEmitterMovOps Emitter = {
      &Assembler::movups, &Assembler::movups, &Assembler::movups};
  emitIASMovlikeXMM(Func, Dest, Src, Emitter);
}

void InstX86Movq::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 1);
  assert(this->getDest()->getType() == IceType_i64 ||
         this->getDest()->getType() == IceType_f64);
  Str << "\t"
         "movq"
         "\t";
  this->getSrc(0)->emit(Func);
  Str << ", ";
  this->getDest()->emit(Func);
}

void InstX86Movq::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 1);
  assert(this->getDest()->getType() == IceType_i64 ||
         this->getDest()->getType() == IceType_f64 ||
         isVectorType(this->getDest()->getType()));
  const Variable *Dest = this->getDest();
  const Operand *Src = this->getSrc(0);
  static const XmmEmitterMovOps Emitter = {&Assembler::movq, &Assembler::movq,
                                           &Assembler::movq};
  emitIASMovlikeXMM(Func, Dest, Src, Emitter);
}

void InstX86MovssRegs::emitIAS(const Cfg *Func) const {
  // This is Binop variant is only intended to be used for reg-reg moves where
  // part of the Dest register is untouched.
  assert(this->getSrcSize() == 2);
  const Variable *Dest = this->getDest();
  assert(Dest == this->getSrc(0));
  const auto *SrcVar = llvm::cast<Variable>(this->getSrc(1));
  assert(Dest->hasReg() && SrcVar->hasReg());
  Assembler *Asm = Func->getAssembler<Assembler>();
  Asm->movss(IceType_f32, Traits::getEncodedXmm(Dest->getRegNum()),
             Traits::getEncodedXmm(SrcVar->getRegNum()));
}

void InstX86Movsx::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 1);
  const Variable *Dest = this->getDest();
  const Operand *Src = this->getSrc(0);
  // Dest must be a > 8-bit register, but Src can be 8-bit. In practice we just
  // use the full register for Dest to avoid having an OperandSizeOverride
  // prefix. It also allows us to only dispatch on SrcTy.
  Type SrcTy = Src->getType();
  assert(typeWidthInBytes(Dest->getType()) > 1);
  assert(typeWidthInBytes(Dest->getType()) > typeWidthInBytes(SrcTy));
  constexpr bool NotLea = false;
  emitIASRegOpTyGPR<false, true>(Func, NotLea, SrcTy, Dest, Src, this->Emitter);
}

bool InstX86Movzx::mayBeElided(const Variable *Dest,
                               const Operand *SrcOpnd) const {
  assert(Traits::Is64Bit);
  const auto *Src = llvm::dyn_cast<Variable>(SrcOpnd);

  // Src is not a Variable, so it does not have a register. Movzx can't be
  // elided.
  if (Src == nullptr)
    return false;

  // Movzx to/from memory can't be elided.
  if (!Src->hasReg() || !Dest->hasReg())
    return false;

  // Reg/reg move with different source and dest can't be elided.
  if (Traits::getEncodedGPR(Src->getRegNum()) !=
      Traits::getEncodedGPR(Dest->getRegNum()))
    return false;

  // A must-keep movzx 32- to 64-bit is sometimes needed in x86-64 sandboxing.
  return !MustKeep;
}

void InstX86Movzx::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  if (Traits::Is64Bit) {
    // There's no movzx %eXX, %rXX. To zero extend 32- to 64-bits, we emit a
    // mov %eXX, %eXX. The processor will still do a movzx[bw]q.
    assert(this->getSrcSize() == 1);
    const Operand *Src = this->getSrc(0);
    const Variable *Dest = this->Dest;
    if (Src->getType() == IceType_i32 && Dest->getType() == IceType_i64) {
      Ostream &Str = Func->getContext()->getStrEmit();
      if (mayBeElided(Dest, Src)) {
        Str << "\t/* elided movzx */";
      } else {
        Str << "\t"
               "mov"
               "\t";
        Src->emit(Func);
        Str << ", ";
        Dest->asType(Func, IceType_i32,
                     Traits::getGprForType(IceType_i32, Dest->getRegNum()))
            ->emit(Func);
        Str << " /* movzx */";
      }
      return;
    }
  }
  InstX86BaseUnaryopGPR<InstX86Base::Movzx>::emit(Func);
}

void InstX86Movzx::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 1);
  const Variable *Dest = this->getDest();
  const Operand *Src = this->getSrc(0);
  Type SrcTy = Src->getType();
  assert(typeWidthInBytes(Dest->getType()) > 1);
  assert(typeWidthInBytes(Dest->getType()) > typeWidthInBytes(SrcTy));
  if (Traits::Is64Bit) {
    if (Src->getType() == IceType_i32 && Dest->getType() == IceType_i64 &&
        mayBeElided(Dest, Src)) {
      return;
    }
  }
  constexpr bool NotLea = false;
  emitIASRegOpTyGPR<false, true>(Func, NotLea, SrcTy, Dest, Src, this->Emitter);
}

void InstX86Nop::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  // TODO: Emit the right code for each variant.
  Str << "\t"
         "nop\t/* variant = "
      << Variant << " */";
}

void InstX86Nop::emitIAS(const Cfg *Func) const {
  Assembler *Asm = Func->getAssembler<Assembler>();
  // TODO: Emit the right code for the variant.
  Asm->nop();
}

void InstX86Nop::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "nop (variant = " << Variant << ")";
}

void InstX86Fld::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 1);
  Type Ty = this->getSrc(0)->getType();
  const auto *Var = llvm::dyn_cast<Variable>(this->getSrc(0));
  if (Var && Var->hasReg()) {
    // This is a physical xmm register, so we need to spill it to a temporary
    // stack slot.  Function prolog emission guarantees that there is sufficient
    // space to do this.
    Str << "\t"
           "mov"
        << Traits::TypeAttributes[Ty].SdSsString << "\t";
    Var->emit(Func);
    Str << ", (%esp)\n"
           "\t"
           "fld"
        << this->getFldString(Ty)
        << "\t"
           "(%esp)";
    return;
  }
  Str << "\t"
         "fld"
      << this->getFldString(Ty) << "\t";
  this->getSrc(0)->emit(Func);
}

void InstX86Fld::emitIAS(const Cfg *Func) const {
  Assembler *Asm = Func->getAssembler<Assembler>();
  assert(this->getSrcSize() == 1);
  const Operand *Src = this->getSrc(0);
  auto *Target = InstX86Base::getTarget(Func);
  Type Ty = Src->getType();
  if (const auto *Var = llvm::dyn_cast<Variable>(Src)) {
    if (Var->hasReg()) {
      // This is a physical xmm register, so we need to spill it to a temporary
      // stack slot.  Function prolog emission guarantees that there is
      // sufficient space to do this.
      Address StackSlot =
          Address(RegisterSet::Encoded_Reg_esp, 0, AssemblerFixup::NoFixup);
      Asm->movss(Ty, StackSlot, Traits::getEncodedXmm(Var->getRegNum()));
      Asm->fld(Ty, StackSlot);
    } else {
      Address StackAddr(Target->stackVarToAsmOperand(Var));
      Asm->fld(Ty, StackAddr);
    }
  } else if (const auto *Mem = llvm::dyn_cast<X86OperandMem>(Src)) {
    assert(Mem->getSegmentRegister() == X86OperandMem::DefaultSegment);
    Asm->fld(Ty, Mem->toAsmAddress(Asm, Target));
  } else if (const auto *Imm = llvm::dyn_cast<Constant>(Src)) {
    Asm->fld(Ty, Traits::Address::ofConstPool(Asm, Imm));
  } else {
    llvm_unreachable("Unexpected operand type");
  }
}

void InstX86Fld::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "fld." << this->getSrc(0)->getType() << " ";
  this->dumpSources(Func);
}

void InstX86Fstp::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 0);
  // TODO(jvoung,stichnot): Utilize this by setting Dest to nullptr to
  // "partially" delete the fstp if the Dest is unused. Even if Dest is unused,
  // the fstp should be kept for the SideEffects of popping the stack.
  if (!this->getDest()) {
    Str << "\t"
           "fstp\t"
           "st(0)";
    return;
  }
  Type Ty = this->getDest()->getType();
  if (!this->getDest()->hasReg()) {
    Str << "\t"
           "fstp"
        << this->getFldString(Ty) << "\t";
    this->getDest()->emit(Func);
    return;
  }
  // Dest is a physical (xmm) register, so st(0) needs to go through memory.
  // Hack this by using caller-reserved memory at the top of stack, spilling
  // st(0) there, and loading it into the xmm register.
  Str << "\t"
         "fstp"
      << this->getFldString(Ty)
      << "\t"
         "(%esp)\n";
  Str << "\t"
         "mov"
      << Traits::TypeAttributes[Ty].SdSsString
      << "\t"
         "(%esp), ";
  this->getDest()->emit(Func);
}

void InstX86Fstp::emitIAS(const Cfg *Func) const {
  Assembler *Asm = Func->getAssembler<Assembler>();
  assert(this->getSrcSize() == 0);
  const Variable *Dest = this->getDest();
  // TODO(jvoung,stichnot): Utilize this by setting Dest to nullptr to
  // "partially" delete the fstp if the Dest is unused. Even if Dest is unused,
  // the fstp should be kept for the SideEffects of popping the stack.
  if (!Dest) {
    Asm->fstp(RegisterSet::getEncodedSTReg(0));
    return;
  }
  auto *Target = InstX86Base::getTarget(Func);
  Type Ty = Dest->getType();
  if (!Dest->hasReg()) {
    Address StackAddr(Target->stackVarToAsmOperand(Dest));
    Asm->fstp(Ty, StackAddr);
  } else {
    // Dest is a physical (xmm) register, so st(0) needs to go through memory.
    // Hack this by using caller-reserved memory at the top of stack, spilling
    // st(0) there, and loading it into the xmm register.
    Address StackSlot =
        Address(RegisterSet::Encoded_Reg_esp, 0, AssemblerFixup::NoFixup);
    Asm->fstp(Ty, StackSlot);
    Asm->movss(Ty, Traits::getEncodedXmm(Dest->getRegNum()), StackSlot);
  }
}

void InstX86Fstp::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  this->dumpDest(Func);
  Str << " = fstp." << this->getDest()->getType() << ", st(0)";
}

void InstX86Pextr::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 2);
  // pextrb and pextrd are SSE4.1 instructions.
  Str << "\t" << this->Opcode
      << Traits::TypeAttributes[this->getSrc(0)->getType()].IntegralString
      << "\t";
  this->getSrc(1)->emit(Func);
  Str << ", ";
  this->getSrc(0)->emit(Func);
  Str << ", ";
  Variable *Dest = this->getDest();
  // pextrw must take a register dest. There is an SSE4.1 version that takes a
  // memory dest, but we aren't using it. For uniformity, just restrict them
  // all to have a register dest for now.
  assert(Dest->hasReg());
  Dest->asType(Func, IceType_i32, Dest->getRegNum())->emit(Func);
}

void InstX86Pextr::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 2);
  // pextrb and pextrd are SSE4.1 instructions.
  const Variable *Dest = this->getDest();
  Type DispatchTy = Traits::getInVectorElementType(this->getSrc(0)->getType());
  // pextrw must take a register dest. There is an SSE4.1 version that takes a
  // memory dest, but we aren't using it. For uniformity, just restrict them
  // all to have a register dest for now.
  assert(Dest->hasReg());
  // pextrw's Src(0) must be a register (both SSE4.1 and SSE2).
  assert(llvm::cast<Variable>(this->getSrc(0))->hasReg());
  static const ThreeOpImmEmitter<GPRRegister, XmmRegister> Emitter = {
      &Assembler::pextr, nullptr};
  emitIASThreeOpImmOps<GPRRegister, XmmRegister, Traits::getEncodedGPR,
                       Traits::getEncodedXmm>(
      Func, DispatchTy, Dest, this->getSrc(0), this->getSrc(1), Emitter);
}

void InstX86Pinsr::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 3);
  Str << "\t" << this->Opcode
      << Traits::TypeAttributes[this->getDest()->getType()].IntegralString
      << "\t";
  this->getSrc(2)->emit(Func);
  Str << ", ";
  Operand *Src1 = this->getSrc(1);
  if (const auto *Src1Var = llvm::dyn_cast<Variable>(Src1)) {
    // If src1 is a register, it should always be r32.
    if (Src1Var->hasReg()) {
      const auto NewRegNum = Traits::getBaseReg(Src1Var->getRegNum());
      const Variable *NewSrc = Src1Var->asType(Func, IceType_i32, NewRegNum);
      NewSrc->emit(Func);
    } else {
      Src1Var->emit(Func);
    }
  } else {
    Src1->emit(Func);
  }
  Str << ", ";
  this->getDest()->emit(Func);
}

void InstX86Pinsr::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 3);
  assert(this->getDest() == this->getSrc(0));
  // pinsrb and pinsrd are SSE4.1 instructions.
  const Operand *Src0 = this->getSrc(1);
  Type DispatchTy = Src0->getType();
  // If src1 is a register, it should always be r32 (this should fall out from
  // the encodings for ByteRegs overlapping the encodings for r32), but we have
  // to make sure the register allocator didn't choose an 8-bit high register
  // like "ah".
  if (BuildDefs::asserts()) {
    if (auto *Src0Var = llvm::dyn_cast<Variable>(Src0)) {
      if (Src0Var->hasReg()) {
        const auto RegNum = Src0Var->getRegNum();
        const auto BaseRegNum = Traits::getBaseReg(RegNum);
        (void)BaseRegNum;
        assert(Traits::getEncodedGPR(RegNum) ==
               Traits::getEncodedGPR(BaseRegNum));
      }
    }
  }
  static const ThreeOpImmEmitter<XmmRegister, GPRRegister> Emitter = {
      &Assembler::pinsr, &Assembler::pinsr};
  emitIASThreeOpImmOps<XmmRegister, GPRRegister, Traits::getEncodedXmm,
                       Traits::getEncodedGPR>(Func, DispatchTy, this->getDest(),
                                              Src0, this->getSrc(2), Emitter);
}

void InstX86Pshufd::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 2);
  const Variable *Dest = this->getDest();
  Type Ty = Dest->getType();
  static const ThreeOpImmEmitter<XmmRegister, XmmRegister> Emitter = {
      &Assembler::pshufd, &Assembler::pshufd};
  emitIASThreeOpImmOps<XmmRegister, XmmRegister, Traits::getEncodedXmm,
                       Traits::getEncodedXmm>(Func, Ty, Dest, this->getSrc(0),
                                              this->getSrc(1), Emitter);
}

void InstX86Shufps::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 3);
  const Variable *Dest = this->getDest();
  assert(Dest == this->getSrc(0));
  Type Ty = Dest->getType();
  static const ThreeOpImmEmitter<XmmRegister, XmmRegister> Emitter = {
      &Assembler::shufps, &Assembler::shufps};
  emitIASThreeOpImmOps<XmmRegister, XmmRegister, Traits::getEncodedXmm,
                       Traits::getEncodedXmm>(Func, Ty, Dest, this->getSrc(1),
                                              this->getSrc(2), Emitter);
}

void InstX86Pop::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(this->getSrcSize() == 0);
  Str << "\t"
         "pop\t";
  this->getDest()->emit(Func);
}

void InstX86Pop::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 0);
  Assembler *Asm = Func->getAssembler<Assembler>();
  if (this->getDest()->hasReg()) {
    Asm->popl(Traits::getEncodedGPR(this->getDest()->getRegNum()));
  } else {
    auto *Target = InstX86Base::getTarget(Func);
    Asm->popl(Target->stackVarToAsmOperand(this->getDest()));
  }
}

void InstX86Pop::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  this->dumpDest(Func);
  Str << " = pop." << this->getDest()->getType() << " ";
}

void InstX86Push::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  Str << "\t"
         "push"
         "\t";
  assert(this->getSrcSize() == 1);
  const Operand *Src = this->getSrc(0);
  Src->emit(Func);
}

void InstX86Push::emitIAS(const Cfg *Func) const {
  Assembler *Asm = Func->getAssembler<Assembler>();

  assert(this->getSrcSize() == 1);
  const Operand *Src = this->getSrc(0);

  if (const auto *Var = llvm::dyn_cast<Variable>(Src)) {
    Asm->pushl(Traits::getEncodedGPR(Var->getRegNum()));
  } else if (const auto *Const32 = llvm::dyn_cast<ConstantInteger32>(Src)) {
    Asm->pushl(AssemblerImmediate(Const32->getValue()));
  } else if (auto *CR = llvm::dyn_cast<ConstantRelocatable>(Src)) {
    Asm->pushl(CR);
  } else {
    llvm_unreachable("Unexpected operand type");
  }
}

void InstX86Push::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "push." << this->getSrc(0)->getType() << " ";
  this->dumpSources(Func);
}

void InstX86Ret::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  Str << "\t"
         "ret";
}

void InstX86Ret::emitIAS(const Cfg *Func) const {
  Assembler *Asm = Func->getAssembler<Assembler>();
  Asm->ret();
}

void InstX86Ret::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Type Ty =
      (this->getSrcSize() == 0 ? IceType_void : this->getSrc(0)->getType());
  Str << "ret." << Ty << " ";
  this->dumpSources(Func);
}

void InstX86Setcc::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  Str << "\t"
         "set"
      << Traits::InstBrAttributes[Condition].DisplayString << "\t";
  this->Dest->emit(Func);
}

void InstX86Setcc::emitIAS(const Cfg *Func) const {
  assert(Condition != Cond::Br_None);
  assert(this->getDest()->getType() == IceType_i1);
  assert(this->getSrcSize() == 0);
  Assembler *Asm = Func->getAssembler<Assembler>();
  auto *Target = InstX86Base::getTarget(Func);
  if (this->getDest()->hasReg())
    Asm->setcc(Condition,
               Traits::getEncodedByteReg(this->getDest()->getRegNum()));
  else
    Asm->setcc(Condition, Target->stackVarToAsmOperand(this->getDest()));
  return;
}

void InstX86Setcc::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "setcc." << Traits::InstBrAttributes[Condition].DisplayString << " ";
  this->dumpDest(Func);
}

void InstX86Xadd::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  if (this->Locked) {
    Str << "\t"
           "lock";
  }
  Str << "\t"
         "xadd"
      << this->getWidthString(this->getSrc(0)->getType()) << "\t";
  this->getSrc(1)->emit(Func);
  Str << ", ";
  this->getSrc(0)->emit(Func);
}

void InstX86Xadd::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 2);
  Assembler *Asm = Func->getAssembler<Assembler>();
  Type Ty = this->getSrc(0)->getType();
  const auto Mem = llvm::cast<X86OperandMem>(this->getSrc(0));
  assert(Mem->getSegmentRegister() == X86OperandMem::DefaultSegment);
  auto *Target = InstX86Base::getTarget(Func);
  const Address Addr = Mem->toAsmAddress(Asm, Target);
  const auto *VarReg = llvm::cast<Variable>(this->getSrc(1));
  assert(VarReg->hasReg());
  const GPRRegister Reg = Traits::getEncodedGPR(VarReg->getRegNum());
  Asm->xadd(Ty, Addr, Reg, this->Locked);
}

void InstX86Xadd::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  if (this->Locked) {
    Str << "lock ";
  }
  Type Ty = this->getSrc(0)->getType();
  Str << "xadd." << Ty << " ";
  this->dumpSources(Func);
}

void InstX86Xchg::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  Str << "\t"
         "xchg"
      << this->getWidthString(this->getSrc(0)->getType()) << "\t";
  this->getSrc(1)->emit(Func);
  Str << ", ";
  this->getSrc(0)->emit(Func);
}

void InstX86Xchg::emitIAS(const Cfg *Func) const {
  assert(this->getSrcSize() == 2);
  Assembler *Asm = Func->getAssembler<Assembler>();
  Type Ty = this->getSrc(0)->getType();
  const auto *VarReg1 = llvm::cast<Variable>(this->getSrc(1));
  assert(VarReg1->hasReg());
  const GPRRegister Reg1 = Traits::getEncodedGPR(VarReg1->getRegNum());

  if (const auto *VarReg0 = llvm::dyn_cast<Variable>(this->getSrc(0))) {
    assert(VarReg0->hasReg());
    const GPRRegister Reg0 = Traits::getEncodedGPR(VarReg0->getRegNum());
    Asm->xchg(Ty, Reg0, Reg1);
    return;
  }

  const auto *Mem = llvm::cast<X86OperandMem>(this->getSrc(0));
  assert(Mem->getSegmentRegister() == X86OperandMem::DefaultSegment);
  auto *Target = InstX86Base::getTarget(Func);
  const Address Addr = Mem->toAsmAddress(Asm, Target);
  Asm->xchg(Ty, Addr, Reg1);
}

void InstX86Xchg::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Type Ty = this->getSrc(0)->getType();
  Str << "xchg." << Ty << " ";
  this->dumpSources(Func);
}

void InstX86IacaStart::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  Str << "\t# IACA_START\n"
         "\t.byte 0x0F, 0x0B\n"
         "\t"
         "movl\t$111, %ebx\n"
         "\t.byte 0x64, 0x67, 0x90";
}

void InstX86IacaStart::emitIAS(const Cfg *Func) const {
  Assembler *Asm = Func->getAssembler<Assembler>();
  Asm->iaca_start();
}

void InstX86IacaStart::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "IACA_START";
}

void InstX86IacaEnd::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  Str << "\t# IACA_END\n"
         "\t"
         "movl\t$222, %ebx\n"
         "\t.byte 0x64, 0x67, 0x90\n"
         "\t.byte 0x0F, 0x0B";
}

void InstX86IacaEnd::emitIAS(const Cfg *Func) const {
  Assembler *Asm = Func->getAssembler<Assembler>();
  Asm->iaca_end();
}

void InstX86IacaEnd::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "IACA_END";
}

const TargetX8632Traits::InstBrAttributesType
    TargetX8632Traits::InstBrAttributes[] = {
#define X(val, encode, opp, dump, emit) {CondX86::opp, dump, emit},
        ICEINSTX86BR_TABLE
#undef X
};

const TargetX8632Traits::InstCmppsAttributesType
    TargetX8632Traits::InstCmppsAttributes[] = {
#define X(val, emit) {emit},
        ICEINSTX86CMPPS_TABLE
#undef X
};

const TargetX8632Traits::TypeAttributesType
    TargetX8632Traits::TypeAttributes[] = {
#define X(tag, elty, cvt, sdss, pdps, spsd, int_, unpack, pack, width, fld)    \
  {cvt, sdss, pdps, spsd, int_, unpack, pack, width, fld},
        ICETYPEX86_TABLE
#undef X
};

const char *TargetX8632Traits::InstSegmentRegNames[] = {
#define X(val, name, prefix) name,
    SEG_REGX8632_TABLE
#undef X
};

uint8_t TargetX8632Traits::InstSegmentPrefixes[] = {
#define X(val, name, prefix) prefix,
    SEG_REGX8632_TABLE
#undef X
};

void TargetX8632Traits::X86Operand::dump(const Cfg *, Ostream &Str) const {
  if (BuildDefs::dump())
    Str << "<OperandX8632>";
}

TargetX8632Traits::X86OperandMem::X86OperandMem(
    Cfg *Func, Type Ty, Variable *Base, Constant *Offset, Variable *Index,
    uint16_t Shift, SegmentRegisters SegmentReg, bool IsRebased)
    : X86Operand(kMem, Ty), Base(Base), Offset(Offset), Index(Index),
      Shift(Shift), SegmentReg(SegmentReg), IsRebased(IsRebased) {
  assert(Shift <= 3);
  Vars = nullptr;
  NumVars = 0;
  if (Base)
    ++NumVars;
  if (Index)
    ++NumVars;
  if (NumVars) {
    Vars = Func->allocateArrayOf<Variable *>(NumVars);
    SizeT I = 0;
    if (Base)
      Vars[I++] = Base;
    if (Index)
      Vars[I++] = Index;
    assert(I == NumVars);
  }
}

namespace {

int32_t getRematerializableOffset(Variable *Var,
                                  const Ice::X8632::TargetX8632 *Target) {
  int32_t Disp = Var->getStackOffset();
  const auto RegNum = Var->getRegNum();
  if (RegNum == Target->getFrameReg()) {
    Disp += Target->getFrameFixedAllocaOffset();
  } else if (RegNum != Target->getStackReg()) {
    llvm::report_fatal_error("Unexpected rematerializable register type");
  }
  return Disp;
}

void validateMemOperandPIC(const TargetX8632Traits::X86OperandMem *Mem) {
  if (!BuildDefs::asserts())
    return;
  const bool HasCR =
      Mem->getOffset() && llvm::isa<ConstantRelocatable>(Mem->getOffset());
  (void)HasCR;
  const bool IsRebased = Mem->getIsRebased();
  (void)IsRebased;
  assert(!IsRebased);
}

} // end of anonymous namespace

void TargetX8632Traits::X86OperandMem::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  validateMemOperandPIC(this);
  const auto *Target =
      static_cast<const ::Ice::X8632::TargetX8632 *>(Func->getTarget());
  // If the base is rematerializable, we need to replace it with the correct
  // physical register (esp or ebp), and update the Offset.
  int32_t Disp = 0;
  if (getBase() && getBase()->isRematerializable()) {
    Disp += getRematerializableOffset(getBase(), Target);
  }
  // The index should never be rematerializable.  But if we ever allow it, then
  // we should make sure the rematerialization offset is shifted by the Shift
  // value.
  if (getIndex())
    assert(!getIndex()->isRematerializable());
  Ostream &Str = Func->getContext()->getStrEmit();
  if (SegmentReg != DefaultSegment) {
    assert(SegmentReg >= 0 && SegmentReg < SegReg_NUM);
    Str << "%" << X8632::Traits::InstSegmentRegNames[SegmentReg] << ":";
  }
  // Emit as Offset(Base,Index,1<<Shift). Offset is emitted without the leading
  // '$'. Omit the (Base,Index,1<<Shift) part if Base==nullptr.
  if (getOffset() == nullptr && Disp == 0) {
    // No offset, emit nothing.
  } else if (getOffset() == nullptr && Disp != 0) {
    Str << Disp;
  } else if (const auto *CI = llvm::dyn_cast<ConstantInteger32>(getOffset())) {
    if (getBase() == nullptr || CI->getValue() || Disp != 0)
      // Emit a non-zero offset without a leading '$'.
      Str << CI->getValue() + Disp;
  } else if (const auto *CR =
                 llvm::dyn_cast<ConstantRelocatable>(getOffset())) {
    // TODO(sehr): ConstantRelocatable still needs updating for
    // rematerializable base/index and Disp.
    assert(Disp == 0);
    CR->emitWithoutPrefix(Target);
  } else {
    llvm_unreachable("Invalid offset type for x86 mem operand");
  }

  if (getBase() || getIndex()) {
    Str << "(";
    if (getBase())
      getBase()->emit(Func);
    if (getIndex()) {
      Str << ",";
      getIndex()->emit(Func);
      if (getShift())
        Str << "," << (1u << getShift());
    }
    Str << ")";
  }
}

void TargetX8632Traits::X86OperandMem::dump(const Cfg *Func,
                                            Ostream &Str) const {
  if (!BuildDefs::dump())
    return;
  if (SegmentReg != DefaultSegment) {
    assert(SegmentReg >= 0 && SegmentReg < SegReg_NUM);
    Str << X8632::Traits::InstSegmentRegNames[SegmentReg] << ":";
  }
  bool Dumped = false;
  Str << "[";
  int32_t Disp = 0;
  const auto *Target =
      static_cast<const ::Ice::X8632::TargetX8632 *>(Func->getTarget());
  if (getBase() && getBase()->isRematerializable()) {
    Disp += getRematerializableOffset(getBase(), Target);
  }
  if (getBase()) {
    if (Func)
      getBase()->dump(Func);
    else
      getBase()->dump(Str);
    Dumped = true;
  }
  if (getIndex()) {
    assert(!getIndex()->isRematerializable());
    if (getBase())
      Str << "+";
    if (getShift() > 0)
      Str << (1u << getShift()) << "*";
    if (Func)
      getIndex()->dump(Func);
    else
      getIndex()->dump(Str);
    Dumped = true;
  }
  if (Disp) {
    if (Disp > 0)
      Str << "+";
    Str << Disp;
    Dumped = true;
  }
  // Pretty-print the Offset.
  bool OffsetIsZero = false;
  bool OffsetIsNegative = false;
  if (getOffset() == nullptr) {
    OffsetIsZero = true;
  } else if (const auto *CI = llvm::dyn_cast<ConstantInteger32>(getOffset())) {
    OffsetIsZero = (CI->getValue() == 0);
    OffsetIsNegative = (static_cast<int32_t>(CI->getValue()) < 0);
  } else {
    assert(llvm::isa<ConstantRelocatable>(getOffset()));
  }
  if (Dumped) {
    if (!OffsetIsZero) {     // Suppress if Offset is known to be 0
      if (!OffsetIsNegative) // Suppress if Offset is known to be negative
        Str << "+";
      getOffset()->dump(Func, Str);
    }
  } else {
    // There is only the offset.
    getOffset()->dump(Func, Str);
  }
  Str << "]";
}

void TargetX8632Traits::X86OperandMem::emitSegmentOverride(
    TargetX8632Traits::Assembler *Asm) const {
  if (SegmentReg != DefaultSegment) {
    assert(SegmentReg >= 0 && SegmentReg < SegReg_NUM);
    Asm->emitSegmentOverride(X8632::Traits::InstSegmentPrefixes[SegmentReg]);
  }
}

TargetX8632Traits::Address TargetX8632Traits::X86OperandMem::toAsmAddress(
    TargetX8632Traits::Assembler *Asm,
    const Ice::TargetLowering *TargetLowering, bool /*IsLeaAddr*/) const {
  const auto *Target =
      static_cast<const ::Ice::X8632::TargetX8632 *>(TargetLowering);
  validateMemOperandPIC(this);
  int32_t Disp = 0;
  if (getBase() && getBase()->isRematerializable()) {
    Disp += getRematerializableOffset(getBase(), Target);
  }
  // The index should never be rematerializable.  But if we ever allow it, then
  // we should make sure the rematerialization offset is shifted by the Shift
  // value.
  if (getIndex())
    assert(!getIndex()->isRematerializable());
  AssemblerFixup *Fixup = nullptr;
  // Determine the offset (is it relocatable?)
  if (getOffset()) {
    if (const auto *CI = llvm::dyn_cast<ConstantInteger32>(getOffset())) {
      Disp += static_cast<int32_t>(CI->getValue());
    } else if (const auto CR =
                   llvm::dyn_cast<ConstantRelocatable>(getOffset())) {
      Disp += CR->getOffset();
      Fixup = Asm->createFixup(Target->getAbsFixup(), CR);
    } else {
      llvm_unreachable("Unexpected offset type");
    }
  }

  // Now convert to the various possible forms.
  if (getBase() && getIndex()) {
    return X8632::Traits::Address(getEncodedGPR(getBase()->getRegNum()),
                                  getEncodedGPR(getIndex()->getRegNum()),
                                  X8632::Traits::ScaleFactor(getShift()), Disp,
                                  Fixup);
  } else if (getBase()) {
    return X8632::Traits::Address(getEncodedGPR(getBase()->getRegNum()), Disp,
                                  Fixup);
  } else if (getIndex()) {
    return X8632::Traits::Address(getEncodedGPR(getIndex()->getRegNum()),
                                  X8632::Traits::ScaleFactor(getShift()), Disp,
                                  Fixup);
  } else {
    return X8632::Traits::Address(Disp, Fixup);
  }
}

TargetX8632Traits::Address
TargetX8632Traits::VariableSplit::toAsmAddress(const Cfg *Func) const {
  assert(!Var->hasReg());
  const ::Ice::TargetLowering *Target = Func->getTarget();
  int32_t Offset = Var->getStackOffset() + getOffset();
  return X8632::Traits::Address(getEncodedGPR(Target->getFrameOrStackReg()),
                                Offset, AssemblerFixup::NoFixup);
}

void TargetX8632Traits::VariableSplit::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(!Var->hasReg());
  // The following is copied/adapted from TargetX8632::emitVariable().
  const ::Ice::TargetLowering *Target = Func->getTarget();
  constexpr Type Ty = IceType_i32;
  int32_t Offset = Var->getStackOffset() + getOffset();
  if (Offset)
    Str << Offset;
  Str << "(%" << Target->getRegName(Target->getFrameOrStackReg(), Ty) << ")";
}

void TargetX8632Traits::VariableSplit::dump(const Cfg *Func,
                                            Ostream &Str) const {
  if (!BuildDefs::dump())
    return;
  switch (Part) {
  case Low:
    Str << "low";
    break;
  case High:
    Str << "high";
    break;
  }
  Str << "(";
  if (Func)
    Var->dump(Func);
  else
    Var->dump(Str);
  Str << ")";
}

} // namespace X8632
} // end of namespace Ice
