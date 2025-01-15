#include "BehaviourAI.h"

#include "Sim/Units/Unit.h"

CR_BIND_DERIVED_INTERFACE(CBehaviourAI, CObject)

CR_REG_METADATA(CBehaviourAI, (
	CR_MEMBER(owner),
	CR_PREALLOC(GetPreallocContainer)
))

CBehaviourAI::CBehaviourAI(CUnit *owner):
	owner(owner)
{
}

CBehaviourAI::CBehaviourAI():
	owner(nullptr)
{
}
