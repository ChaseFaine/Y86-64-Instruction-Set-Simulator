#include <string>
#include <cstdint>
#include "RegisterFile.h"
#include "PipeRegField.h"
#include "PipeReg.h"
#include "F.h"
#include "D.h"
#include "M.h"
#include "W.h"
#include "E.h"
#include "Stage.h"
#include "ExecuteStage.h"
#include "Status.h"
#include "Debug.h"
#include "Instructions.h"
#include "ConditionCodes.h"
#include "Tools.h"
#include "MemoryStage.h"


/*
 * doClockLow:
 * Performs the Fetch stage combinational logic that is performed when
 * the clock edge is low.
 *
 * @param: pregs - array of the pipeline register sets (F, D, E, M, W instances)
 * @param: stages - array of stages (FetchStage, DecodeStage, ExecuteStage,
 *         MemoryStage, WritebackStage instances)
 */
bool ExecuteStage::doClockLow(PipeReg ** pregs, Stage ** stages)
{
    M * mreg = (M *) pregs[MREG];
    E * ereg = (E *) pregs[EREG];
    W * wreg = (W *) pregs[WREG];

    uint64_t stat = ereg->getstat()->getOutput();
    uint64_t icode = ereg->geticode()->getOutput();
    uint64_t valA = ereg->getvalA()->getOutput();
    dstE = ereg->getdstE()->getOutput(); 
    uint64_t dstM = ereg->getdstM()->getOutput();
    uint64_t valB = ereg->getvalB()->getOutput();
    uint64_t valC = ereg-> getvalC()->getOutput();
    uint64_t ifun = ereg->getifun()->getOutput();

    MemoryStage * m = (MemoryStage*) stages[MSTAGE];
    uint64_t m_stat = m->getm_stat();
    uint64_t W_stat = wreg->getstat()->getOutput();

	bool of = false;

    valE = ALU(icode, ifun, aluA(icode, valA, valC), aluB(icode, valB), m_stat, W_stat, & of);

	bool error = false;
	uint64_t sign = Tools::sign(valE);
	uint64_t zf = 0;
	if(valE == 0) {
		zf = 1;
	}

	bool changeCC = set_cc(icode, m_stat, W_stat);

	if(changeCC) {
		ConditionCodes * cceditor = ConditionCodes::getInstance();
		cceditor->setConditionCode(sign, SF, error);
		cceditor->setConditionCode(zf, ZF, error);
		cceditor->setConditionCode(of, OF, error); 
	}


    e_Cnd = cond(icode, ifun);
    dstE = e_dstE(icode, dstE, e_Cnd);
    ExecuteStage::setMInput(mreg, stat, icode, e_Cnd, valA, valE, dstE, dstM);

    M_bubble = calculateControlSignals(m_stat, W_stat);

    return false;
}

/*
bubbles the M register
*/
void ExecuteStage::bubbleM(PipeReg ** pregs) {
    M * mreg = (M *) pregs[MREG];
    mreg->getstat()->bubble(SAOK);
    mreg->geticode()->bubble(INOP);
    mreg->getCnd()->bubble();
    mreg->getvalE()->bubble();
    mreg->getvalA()->bubble();
    mreg->getdstE()->bubble(RNONE);
    mreg->getdstM()->bubble(RNONE);
}

/*
normalizes the M register
*/
void ExecuteStage::normalM(PipeReg ** pregs) {
    M * mreg = (M *) pregs[MREG];
    mreg->getstat()->normal();
    mreg->geticode()->normal();
    mreg->getCnd()->normal();
    mreg->getvalE()->normal();
    mreg->getvalA()->normal();
    mreg->getdstE()->normal();
    mreg->getdstM()->normal();
}

/*
getter for execute stage Cnd
*/
uint64_t ExecuteStage::gete_Cnd()
{
    return e_Cnd;
}

/* doClockHigh
 * applies the appropriate control signal to the F
 * and D register intances
 *
 * @param: pregs - array of the pipeline register (F, D, E, M, W instances)
 */
void ExecuteStage::doClockHigh(PipeReg ** pregs)
{
    if (!M_bubble) {
        normalM(pregs);
    }
    else {
        bubbleM(pregs);
    }
}

/*
sets the Memory stage input
*/
void ExecuteStage::setMInput(M * mreg, uint64_t stat, uint64_t icode,
                            uint64_t Cnd, uint64_t valA, uint64_t valE,
                            uint64_t dstE, uint64_t dstM) {
    mreg->getstat()->setInput(stat);
    mreg->geticode()->setInput(icode);
    mreg->getCnd()->setInput(Cnd);
    mreg->getvalA()->setInput(valA);
    mreg->getvalE()->setInput(valE);
    mreg->getdstE()->setInput(dstE);
    mreg->getdstM()->setInput(dstM);
}

/*
calculates the alu A component to be used in the ALU
*/
uint64_t ExecuteStage::aluA(uint64_t icode, uint64_t valA, uint64_t valC) {
    if (icode == IRRMOVQ || icode == IOPQ) {
        return valA;
    }
    if (icode == IIRMOVQ || icode == IRMMOVQ || icode == IMRMOVQ) {
        return valC;
    }
    if (icode == ICALL || icode == IPUSHQ) {
        return -8;
    }
    if (icode == IRET || icode == IPOPQ) {
        return 8;
    }
    return 0;
}

/*
calculates the alu B component to be used in the ALU
*/
uint64_t ExecuteStage::aluB(uint64_t icode, uint64_t valB) {
    if (icode == IRMMOVQ || icode == IMRMOVQ || icode == IOPQ || icode == ICALL
        || icode == IPUSHQ || icode == IRET || icode == IPOPQ) {
            return valB;
    }
    if (icode == IRRMOVQ || icode == IIRMOVQ) {
        return 0;
    }
    return 0;
}

/*
tells us if our function is an IOP
*/
uint64_t ExecuteStage::alufun(uint64_t icode, uint64_t ifun) {
    if (icode == IOPQ) {
        return ifun;
    }
    return ADDQ;
}

/*
tells us if we should set the condition codes or not
*/
bool ExecuteStage::set_cc(uint64_t icode, uint64_t m_stat, uint64_t W_stat) {
    if ((icode == IOPQ) && (m_stat != SADR && m_stat != SINS && m_stat != SHLT)
        && (W_stat != SADR && W_stat != SINS && W_stat != SHLT)) {
        return true;
    }
    return false;
}
/*
calculates the execute stage dstE
*/
uint64_t ExecuteStage::e_dstE(uint64_t icode, uint64_t dstE, uint64_t e_Cnd) {
    if (icode == IRRMOVQ && !e_Cnd) {
        return RNONE;
    }
    return dstE;
}

/*
getter for execute stage dstE
*/
uint64_t ExecuteStage::gete_dstE()
{
    return dstE;
}

/*
getter for execute stage valE
*/
uint64_t ExecuteStage::gete_valE()
{
    return valE;
}
/*
sets condition codes based on input
*/
void ExecuteStage::CC(uint64_t icode, uint64_t ifun, uint64_t op1, uint64_t op2, uint64_t m_stat, uint64_t W_stat) {
    if (set_cc(icode, m_stat, W_stat)) {
        ConditionCodes * codeInstance = ConditionCodes::getInstance();
        bool error;
        if(ifun == ADDQ) {
            bool overFlowADDQ = Tools::addOverflow(op1, op2);
            codeInstance->setConditionCode(overFlowADDQ, OF, error);

            uint64_t signFlagADDQ = Tools::sign(op1 + op2);
            codeInstance->setConditionCode(signFlagADDQ, SF, error);

            if ((op1 + op2) == 0) {
                codeInstance->setConditionCode(1, ZF, error);
            } else {
                codeInstance->setConditionCode(0, ZF, error);
            }
        }
        if (ifun == SUBQ) {
            bool overFlowSUBQ = Tools::subOverflow(op1, op2);
            codeInstance->setConditionCode(overFlowSUBQ, OF, error);

            uint64_t signFlagSUBQ = Tools::sign(op2 - op1);
            codeInstance->setConditionCode(signFlagSUBQ, SF, error);

            if ((op2 - op1) == 0) {
                codeInstance->setConditionCode(1, ZF, error);
            } else {
                codeInstance->setConditionCode(0, ZF, error);
            }
        }
    }
}

/*
acts as the archetectures Arithmetic Logic Unit (ALU)
basically does all the arithmetic operations
*/
uint64_t ExecuteStage::ALU(uint64_t icode, uint64_t ifun, uint64_t aluA, uint64_t aluB, uint64_t m_stat, uint64_t W_stat, bool * of) {
    uint64_t alufun1 = alufun(icode, ifun);
    if (alufun1 == ADDQ) {
	    *of = Tools::addOverflow(aluA, aluB);
        return aluA + aluB;
    }
    else if (alufun1 == SUBQ) {
	    *of = Tools::subOverflow(aluA, aluB);
        return aluB - aluA;
    }
    else if (alufun1 == XORQ) {
        return aluA ^ aluB;
    }
    else {
        return aluA & aluB;
    }
}

/*
will set the value of Cnd if we take a jump or not
*/
uint64_t ExecuteStage::cond(uint64_t icode, uint64_t ifun) {
    ConditionCodes * codeInstance = ConditionCodes::getInstance();
    bool error;
    bool sf = codeInstance->getConditionCode(SF, error);
    bool of = codeInstance->getConditionCode(OF, error);
    bool zf = codeInstance->getConditionCode(ZF, error);
    if (icode == IJXX || icode == ICMOVXX) {
        if (ifun == UNCOND) {
            return 1;
        } else if (ifun == LESSEQ) {
            return ((sf ^ of) | zf);
        } else if (ifun == LESS) {
            return (sf ^ of);
        } else if (ifun == EQUAL) {
            return (zf);
        } else if (ifun == NOTEQUAL) {
            return (!zf);
        } else if (ifun == GREATER) {
            return (!(sf ^ of) & (!zf));
        } else if (ifun == GREATEREQ) {
            return (!(sf ^ of));
        }
    }
    return 0;
}

/*
returns true if the stat is valid for the current instruction
*/
bool ExecuteStage::calculateControlSignals(uint64_t m_stat, uint64_t W_stat) {
    if ((m_stat == SADR || m_stat == SINS || m_stat == SHLT)
        || (W_stat == SADR || W_stat == SINS || W_stat == SHLT)) {
        return true;
    }
    return false;
}