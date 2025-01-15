#include "Behaviour.h"

#include "Sim/Units/Unit.h"

CR_BIND_DERIVED_INTERFACE(CBehaviour, CObject)

CR_REG_METADATA(CBehaviour, (
	CR_MEMBER(owner)
))

CBehaviour::CBehaviour(CUnit *owner):
	owner(owner)
{
}

CBehaviour::CBehaviour():
	owner(nullptr)
{
}
