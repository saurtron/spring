/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */


#include "CobThread.h"
#include "CobFile.h"
#include "CobInstance.h"
#include "CobEngine.h"
#include "Sim/Misc/GlobalConstants.h"
#include "Sim/Misc/GlobalSynced.h"

#include "System/Misc/TracyDefs.h"

CR_BIND(CCobThread, )

CR_REG_METADATA(CCobThread, (
	CR_MEMBER(cobInst),
	CR_IGNORED(cobFile),

	CR_MEMBER(id),
	CR_MEMBER(pc),

	CR_MEMBER(wakeTime),
	CR_MEMBER(paramCount),
	CR_MEMBER(retCode),
	CR_MEMBER(cbParam),
	CR_MEMBER(signalMask),

	CR_MEMBER(waitAxis),
	CR_MEMBER(waitPiece),

	CR_IGNORED(errorCounter),

	CR_MEMBER(cbType),
	CR_MEMBER(state),

	CR_MEMBER(luaArgs),
	CR_MEMBER(callStack),
	CR_MEMBER(dataStack)
))

CR_BIND(CCobThread::CallInfo,)

CR_REG_METADATA_SUB(CCobThread, CallInfo,(
	CR_MEMBER(functionId),
	CR_MEMBER(returnAddr),
	CR_MEMBER(stackTop)
))

std::vector<decltype(CCobThread::dataStack)> CCobThread::freeDataStacks;
std::vector<decltype(CCobThread::callStack)> CCobThread::freeCallStacks;

CCobThread::CCobThread(CCobInstance* _cobInst)
	: cobInst(_cobInst)
	, cobFile(_cobInst->cobFile)
{
	// If there are any free data and call stacks available, reuse them by 
	// moving them to the current thread's data and call stack variables to
	// amortize memory allocations.
	if (!freeDataStacks.empty()) {
		assert(freeDataStacks.size() == freeCallStacks.size());
		dataStack = std::move(freeDataStacks.back());
		freeDataStacks.pop_back();
		callStack = std::move(freeCallStacks.back());
		freeCallStacks.pop_back();
	} else {
    	// These reservation sizes were experimentally obtained from a few
    	// games in BAR, but regardless of the game being played, the size of
    	// all stacks in use will over time converge to the max size because we
    	// are reusing vectors from older threads.
		dataStack.reserve(16);
		callStack.reserve(4);
	}
	memset(&luaArgs[0], 0, MAX_LUA_COB_ARGS * sizeof(luaArgs[0]));
}

CCobThread::~CCobThread()
{
	RECOIL_DETAILED_TRACY_ZONE;
	Stop();

	if (dataStack.capacity() > 0) {
		dataStack.clear();
		freeDataStacks.emplace_back(std::move(dataStack));
		callStack.clear();
		freeCallStacks.emplace_back(std::move(callStack));
	}
}

CCobThread& CCobThread::operator = (CCobThread&& t) {
	id = t.id;
	pc = t.pc;

	wakeTime = t.wakeTime;
	paramCount = t.paramCount;
	retCode = t.retCode;
	cbParam = t.cbParam;
	signalMask = t.signalMask;

	waitAxis = t.waitAxis;
	waitPiece = t.waitPiece;

	std::memcpy(luaArgs, t.luaArgs, sizeof(luaArgs));

	callStack = std::move(t.callStack);
	dataStack = std::move(t.dataStack);
	// execTrace = std::move(t.execTrace);

	state = t.state;
	cbType = t.cbType;

	cobInst = t.cobInst; t.cobInst = nullptr;
	cobFile = t.cobFile; t.cobFile = nullptr;
	return *this;
}

CCobThread& CCobThread::operator = (const CCobThread& t) {
	id = t.id;
	pc = t.pc;

	wakeTime = t.wakeTime;
	paramCount = t.paramCount;
	retCode = t.retCode;
	cbParam = t.cbParam;
	signalMask = t.signalMask;

	waitAxis = t.waitAxis;
	waitPiece = t.waitPiece;

	std::memcpy(luaArgs, t.luaArgs, sizeof(luaArgs));

	callStack = t.callStack;
	dataStack = t.dataStack;
	// execTrace = t.execTrace;

	state = t.state;
	cbType = t.cbType;

	cobInst = t.cobInst;
	cobFile = t.cobFile;
	return *this;
}


void CCobThread::Start(int functionId, int sigMask, const std::array<int, 1 + MAX_COB_ARGS>& args, bool schedule)
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(callStack.size() == 0);

	state = Run;
	pc = cobFile->scriptOffsets[functionId];

	paramCount = args[0];
	signalMask = sigMask;

	CallInfo& ci = PushCallStackRef();
	ci.functionId = functionId;
	ci.returnAddr = -1;
	ci.stackTop   = 0;

	// copy arguments; args[0] holds the count
	// handled by InitStack if thread has a parent that STARTs it,
	// in which case args[0] is 0 and stack already contains data
	if (paramCount > 0) {
		dataStack.resize(paramCount);
		dataStack.assign(args.begin() + 1, args.begin() + 1 + paramCount);
	}

	// add to scheduler
	if (schedule)
		cobEngine->ScheduleThread(this);
}

void CCobThread::Stop()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (cobInst == nullptr)
		return;

	if (cbType != CCobInstance::CBNone)
		cobInst->ThreadCallback(cbType, retCode, cbParam);

	cobInst->RemoveThreadID(id);
	SetState(Dead);

	cobInst = nullptr;
	cobFile = nullptr;
}


const std::string& CCobThread::GetName()
{
	RECOIL_DETAILED_TRACY_ZONE;
	return cobFile->scriptNames[callStack[0].functionId];
}


int CCobThread::CheckStack(unsigned int size, bool warn)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (size <= dataStack.size())
		return size;

	if (warn) {
		char msg[512];
		const char* fmt =
			"stack-size mismatch: need %u but have %d arguments "
			"(too many passed to function or too few returned?)";

		SNPRINTF(msg, sizeof(msg), fmt, size, dataStack.size());
		ShowError(msg);
	}

	return dataStack.size();
}

void CCobThread::InitStack(unsigned int n, CCobThread* t)
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(dataStack.size() == 0);

	// move n arguments from caller's stack onto our own
	for (unsigned int i = 0; i < n; ++i) {
		PushDataStack(t->PopDataStack());
	}
}



// Command documentation from http://visualta.tauniverse.com/Downloads/cob-commands.txt
// And some information from basm0.8 source (basm ops.txt)

// Model interaction
static constexpr int MOVE       = 0x10001000;
static constexpr int TURN       = 0x10002000;
static constexpr int SPIN       = 0x10003000;
static constexpr int STOP_SPIN  = 0x10004000;
static constexpr int SHOW       = 0x10005000;
static constexpr int HIDE       = 0x10006000;
static constexpr int CACHE      = 0x10007000;
static constexpr int DONT_CACHE = 0x10008000;
static constexpr int MOVE_NOW   = 0x1000B000;
static constexpr int TURN_NOW   = 0x1000C000;
static constexpr int SHADE      = 0x1000D000;
static constexpr int DONT_SHADE = 0x1000E000;
static constexpr int EMIT_SFX   = 0x1000F000;

// Blocking operations
static constexpr int WAIT_TURN  = 0x10011000;
static constexpr int WAIT_MOVE  = 0x10012000;
static constexpr int SLEEP      = 0x10013000;

// Stack manipulation
static constexpr int PUSH_CONSTANT    = 0x10021001;
static constexpr int PUSH_LOCAL_VAR   = 0x10021002;
static constexpr int PUSH_STATIC      = 0x10021004;
static constexpr int CREATE_LOCAL_VAR = 0x10022000;
static constexpr int POP_LOCAL_VAR    = 0x10023002;
static constexpr int POP_STATIC       = 0x10023004;
static constexpr int POP_STACK        = 0x10024000; ///< Not sure what this is supposed to do

// Arithmetic operations
static constexpr int ADD         = 0x10031000;
static constexpr int SUB         = 0x10032000;
static constexpr int MUL         = 0x10033000;
static constexpr int DIV         = 0x10034000;
static constexpr int MOD		  = 0x10034001; ///< spring specific
static constexpr int BITWISE_AND = 0x10035000;
static constexpr int BITWISE_OR  = 0x10036000;
static constexpr int BITWISE_XOR = 0x10037000;
static constexpr int BITWISE_NOT = 0x10038000;

// Native function calls
static constexpr int RAND           = 0x10041000;
static constexpr int GET_UNIT_VALUE = 0x10042000;
static constexpr int GET            = 0x10043000;

// Comparison
static constexpr int SET_LESS             = 0x10051000;
static constexpr int SET_LESS_OR_EQUAL    = 0x10052000;
static constexpr int SET_GREATER          = 0x10053000;
static constexpr int SET_GREATER_OR_EQUAL = 0x10054000;
static constexpr int SET_EQUAL            = 0x10055000;
static constexpr int SET_NOT_EQUAL        = 0x10056000;
static constexpr int LOGICAL_AND          = 0x10057000;
static constexpr int LOGICAL_OR           = 0x10058000;
static constexpr int LOGICAL_XOR          = 0x10059000;
static constexpr int LOGICAL_NOT          = 0x1005A000;

// Flow control
static constexpr int START           = 0x10061000;
static constexpr int CALL            = 0x10062000; ///< converted when executed
static constexpr int REAL_CALL       = 0x10062001; ///< spring custom
static constexpr int LUA_CALL        = 0x10062002; ///< spring custom
static constexpr int JUMP            = 0x10064000;
static constexpr int RETURN          = 0x10065000;
static constexpr int JUMP_NOT_EQUAL  = 0x10066000;
static constexpr int SIGNAL          = 0x10067000;
static constexpr int SET_SIGNAL_MASK = 0x10068000;

// Piece destruction
static constexpr int EXPLODE    = 0x10071000;
static constexpr int PLAY_SOUND = 0x10072000;

// Special functions
static constexpr int SET    = 0x10082000;
static constexpr int ATTACH = 0x10083000;
static constexpr int DROP   = 0x10084000;

// Indices for SET, GET, and GET_UNIT_VALUE for LUA return values
static constexpr int LUA0 = 110; // (LUA0 returns the lua call status, 0 or 1)
static constexpr int LUA1 = 111;
static constexpr int LUA2 = 112;
static constexpr int LUA3 = 113;
static constexpr int LUA4 = 114;
static constexpr int LUA5 = 115;
static constexpr int LUA6 = 116;
static constexpr int LUA7 = 117;
static constexpr int LUA8 = 118;
static constexpr int LUA9 = 119;

#if 0
#define GET_LONG_PC() (cobFile->code[pc++])
#else
// mantis #5981
#define GET_LONG_PC() (cobFile->code.at(pc++))
#endif


#if 0
static const char* GetOpcodeName(int opcode)
{
	switch (opcode) {
		case MOVE: return "move";
		case TURN: return "turn";
		case SPIN: return "spin";
		case STOP_SPIN: return "stop-spin";
		case SHOW: return "show";
		case HIDE: return "hide";
		case CACHE: return "cache";
		case DONT_CACHE: return "dont-cache";
		case TURN_NOW: return "turn-now";
		case MOVE_NOW: return "move-now";
		case SHADE: return "shade";
		case DONT_SHADE: return "dont-shade";
		case EMIT_SFX: return "sfx";

		case WAIT_TURN: return "wait-for-turn";
		case WAIT_MOVE: return "wait-for-move";
		case SLEEP: return "sleep";

		case PUSH_CONSTANT: return "pushc";
		case PUSH_LOCAL_VAR: return "pushl";
		case PUSH_STATIC: return "pushs";
		case CREATE_LOCAL_VAR: return "clv";
		case POP_LOCAL_VAR: return "popl";
		case POP_STATIC: return "pops";
		case POP_STACK: return "pop-stack";

		case ADD: return "add";
		case SUB: return "sub";
		case MUL: return "mul";
		case DIV: return "div";
		case MOD: return "mod";
		case BITWISE_AND: return "and";
		case BITWISE_OR: return "or";
		case BITWISE_XOR: return "xor";
		case BITWISE_NOT: return "not";

		case RAND: return "rand";
		case GET_UNIT_VALUE: return "getuv";
		case GET: return "get";

		case SET_LESS: return "setl";
		case SET_LESS_OR_EQUAL: return "setle";
		case SET_GREATER: return "setg";
		case SET_GREATER_OR_EQUAL: return "setge";
		case SET_EQUAL: return "sete";
		case SET_NOT_EQUAL: return "setne";
		case LOGICAL_AND: return "land";
		case LOGICAL_OR: return "lor";
		case LOGICAL_XOR: return "lxor";
		case LOGICAL_NOT: return "neg";

		case START: return "start";
		case CALL: return "call";
		case REAL_CALL: return "call";
		case LUA_CALL: return "lua_call";
		case JUMP: return "jmp";
		case RETURN: return "return";
		case JUMP_NOT_EQUAL: return "jne";
		case SIGNAL: return "signal";
		case SET_SIGNAL_MASK: return "mask";

		case EXPLODE: return "explode";
		case PLAY_SOUND: return "play-sound";

		case SET: return "set";
		case ATTACH: return "attach";
		case DROP: return "drop";
	}

	return "unknown";
}
#endif


bool CCobThread::Tick()
{
	assert(state != Sleep);
	assert(cobInst != nullptr);

	if (IsDead())
		return false;

	ZoneScoped;

	state = Run;

	int r1, r2, r3, r4, r5, r6;

	while (state == Run) {
		const int opcode = GET_LONG_PC();

		switch (opcode) {
			case PUSH_CONSTANT: {
				r1 = GET_LONG_PC();
				PushDataStack(r1);
			} break;
			case SLEEP: {
				r1 = PopDataStack();
				wakeTime = cobEngine->GetCurrTime() + r1;
				state = Sleep;

				cobEngine->ScheduleThread(this);
				return true;
			} break;
			case SPIN: {
				r1 = GET_LONG_PC();
				r2 = GET_LONG_PC();
				r3 = PopDataStack();         // speed
				r4 = PopDataStack();         // accel
				cobInst->Spin(r1, r2, r3, r4);
			} break;
			case STOP_SPIN: {
				r1 = GET_LONG_PC();
				r2 = GET_LONG_PC();
				r3 = PopDataStack();         // decel

				cobInst->StopSpin(r1, r2, r3);
			} break;
			case RETURN: {
				retCode = PopDataStack();

				if (LocalReturnAddr() == -1) {
					state = Dead;

					// leave values intact on stack in case caller wants to check them
					// callStackSize -= 1;
					return false;
				}

				// return to caller
				pc = LocalReturnAddr();
				if (dataStack.size() > LocalStackFrame())
					dataStack.resize(LocalStackFrame());

				callStack.pop_back();
			} break;


			case SHADE: {
				r1 = GET_LONG_PC();
			} break;
			case DONT_SHADE: {
				r1 = GET_LONG_PC();
			} break;
			case CACHE: {
				r1 = GET_LONG_PC();
			} break;
			case DONT_CACHE: {
				r1 = GET_LONG_PC();
			} break;


			case CALL: {
				r1 = GET_LONG_PC();
				pc--;

				if (cobFile->scriptNames[r1].find("lua_") == 0) {
					cobFile->code[pc - 1] = LUA_CALL;
					LuaCall();
					break;
				}

				cobFile->code[pc - 1] = REAL_CALL;

				// fall-through
			}
			case REAL_CALL: {
				r1 = GET_LONG_PC();
				r2 = GET_LONG_PC();

				// do not call zero-length functions
				if (cobFile->scriptLengths[r1] == 0)
					break;

				CallInfo& ci = PushCallStackRef();
				ci.functionId = r1;
				ci.returnAddr = pc;
				ci.stackTop = dataStack.size() - r2;

				paramCount = r2;

				// call cobFile->scriptNames[r1]
				pc = cobFile->scriptOffsets[r1];
			} break;
			case LUA_CALL: {
				LuaCall();
			} break;


			case POP_STATIC: {
				r1 = GET_LONG_PC();
				r2 = PopDataStack();

				if (static_cast<size_t>(r1) < cobInst->staticVars.size())
					cobInst->staticVars[r1] = r2;
			} break;
			case POP_STACK: {
				PopDataStack();
			} break;


			case START: {
				r1 = GET_LONG_PC();
				r2 = GET_LONG_PC();

				if (cobFile->scriptLengths[r1] == 0)
					break;


				CCobThread t(cobInst);

				t.SetID(cobEngine->GenThreadID());
				t.InitStack(r2, this);
				t.Start(r1, signalMask, {{0}}, true);

				// calling AddThread directly might move <this>, defer it
				cobEngine->QueueAddThread(std::move(t));
			} break;

			case CREATE_LOCAL_VAR: {
				if (paramCount == 0) {
					PushDataStack(0);
				} else {
					paramCount--;
				}
			} break;
			case GET_UNIT_VALUE: {
				r1 = PopDataStack();
				if ((r1 >= LUA0) && (r1 <= LUA9)) {
					PushDataStack(luaArgs[r1 - LUA0]);
					break;
				}
				r1 = cobInst->GetUnitVal(r1, 0, 0, 0, 0);
				PushDataStack(r1);
			} break;


			case JUMP_NOT_EQUAL: {
				r1 = GET_LONG_PC();
				r2 = PopDataStack();

				if (r2 == 0)
					pc = r1;

			} break;
			case JUMP: {
				r1 = GET_LONG_PC();
				// this seem to be an error in the docs..
				//r2 = cobFile->scriptOffsets[LocalFunctionID()] + r1;
				pc = r1;
			} break;


			case POP_LOCAL_VAR: {
				r1 = GET_LONG_PC();
				r2 = PopDataStack();
				dataStack[LocalStackFrame() + r1] = r2;
			} break;
			case PUSH_LOCAL_VAR: {
				r1 = GET_LONG_PC();
				r2 = dataStack[LocalStackFrame() + r1];
				PushDataStack(r2);
			} break;


			case BITWISE_AND: {
				r1 = PopDataStack();
				r2 = PopDataStack();
				PushDataStack(r1 & r2);
			} break;
			case BITWISE_OR: {
				r1 = PopDataStack();
				r2 = PopDataStack();
				PushDataStack(r1 | r2);
			} break;
			case BITWISE_XOR: {
				r1 = PopDataStack();
				r2 = PopDataStack();
				PushDataStack(r1 ^ r2);
			} break;
			case BITWISE_NOT: {
				r1 = PopDataStack();
				PushDataStack(~r1);
			} break;

			case EXPLODE: {
				r1 = GET_LONG_PC();
				r2 = PopDataStack();
				cobInst->Explode(r1, r2);
			} break;

			case PLAY_SOUND: {
				r1 = GET_LONG_PC();
				r2 = PopDataStack();
				cobInst->PlayUnitSound(r1, r2);
			} break;

			case PUSH_STATIC: {
				r1 = GET_LONG_PC();

				if (static_cast<size_t>(r1) < cobInst->staticVars.size())
					PushDataStack(cobInst->staticVars[r1]);
			} break;

			case SET_NOT_EQUAL: {
				r1 = PopDataStack();
				r2 = PopDataStack();

				PushDataStack(int(r1 != r2));
			} break;
			case SET_EQUAL: {
				r1 = PopDataStack();
				r2 = PopDataStack();

				PushDataStack(int(r1 == r2));
			} break;

			case SET_LESS: {
				r2 = PopDataStack();
				r1 = PopDataStack();

				PushDataStack(int(r1 < r2));
			} break;
			case SET_LESS_OR_EQUAL: {
				r2 = PopDataStack();
				r1 = PopDataStack();

				PushDataStack(int(r1 <= r2));
			} break;

			case SET_GREATER: {
				r2 = PopDataStack();
				r1 = PopDataStack();

				PushDataStack(int(r1 > r2));
			} break;
			case SET_GREATER_OR_EQUAL: {
				r2 = PopDataStack();
				r1 = PopDataStack();

				PushDataStack(int(r1 >= r2));
			} break;

			case RAND: {
				r2 = PopDataStack();
				r1 = PopDataStack();
				r3 = gsRNG.NextInt(r2 - r1 + 1) + r1;
				PushDataStack(r3);
			} break;
			case EMIT_SFX: {
				r1 = PopDataStack();
				r2 = GET_LONG_PC();
				cobInst->EmitSfx(r1, r2);
			} break;
			case MUL: {
				r1 = PopDataStack();
				r2 = PopDataStack();
				PushDataStack(r1 * r2);
			} break;


			case SIGNAL: {
				r1 = PopDataStack();
				cobInst->Signal(r1);
			} break;
			case SET_SIGNAL_MASK: {
				r1 = PopDataStack();
				signalMask = r1;
			} break;


			case TURN: {
				r2 = PopDataStack();
				r1 = PopDataStack();
				r3 = GET_LONG_PC(); // piece
				r4 = GET_LONG_PC(); // axis

				cobInst->Turn(r3, r4, r1, r2);
			} break;
			case GET: {
				r5 = PopDataStack();
				r4 = PopDataStack();
				r3 = PopDataStack();
				r2 = PopDataStack();
				r1 = PopDataStack();
				if ((r1 >= LUA0) && (r1 <= LUA9)) {
					PushDataStack(luaArgs[r1 - LUA0]);
					break;
				}
				r6 = cobInst->GetUnitVal(r1, r2, r3, r4, r5);
				PushDataStack(r6);
			} break;
			case ADD: {
				r2 = PopDataStack();
				r1 = PopDataStack();
				PushDataStack(r1 + r2);
			} break;
			case SUB: {
				r2 = PopDataStack();
				r1 = PopDataStack();
				r3 = r1 - r2;
				PushDataStack(r3);
			} break;

			case DIV: {
				r2 = PopDataStack();
				r1 = PopDataStack();

				if (r2 != 0) {
					r3 = r1 / r2;
				} else {
					r3 = 1000; // infinity!
					ShowError("division by zero");
				}
				PushDataStack(r3);
			} break;
			case MOD: {
				r2 = PopDataStack();
				r1 = PopDataStack();

				if (r2 != 0) {
					PushDataStack(r1 % r2);
				} else {
					PushDataStack(0);
					ShowError("modulo division by zero");
				}
			} break;


			case MOVE: {
				r1 = GET_LONG_PC();
				r2 = GET_LONG_PC();
				r4 = PopDataStack();
				r3 = PopDataStack();
				cobInst->Move(r1, r2, r3, r4);
			} break;
			case MOVE_NOW: {
				r1 = GET_LONG_PC();
				r2 = GET_LONG_PC();
				r3 = PopDataStack();
				cobInst->MoveNow(r1, r2, r3);
			} break;
			case TURN_NOW: {
				r1 = GET_LONG_PC();
				r2 = GET_LONG_PC();
				r3 = PopDataStack();
				cobInst->TurnNow(r1, r2, r3);
			} break;


			case WAIT_TURN: {
				r1 = GET_LONG_PC();
				r2 = GET_LONG_PC();

				if (cobInst->NeedsWait(CCobInstance::ATurn, r1, r2)) {
					state = WaitTurn;
					waitPiece = r1;
					waitAxis = r2;
					return true;
				}
			} break;
			case WAIT_MOVE: {
				r1 = GET_LONG_PC();
				r2 = GET_LONG_PC();

				if (cobInst->NeedsWait(CCobInstance::AMove, r1, r2)) {
					state = WaitMove;
					waitPiece = r1;
					waitAxis = r2;
					return true;
				}
			} break;


			case SET: {
				r2 = PopDataStack();
				r1 = PopDataStack();

				if ((r1 >= LUA0) && (r1 <= LUA9)) {
					luaArgs[r1 - LUA0] = r2;
					break;
				}

				cobInst->SetUnitVal(r1, r2);
			} break;


			case ATTACH: {
				r3 = PopDataStack();
				r2 = PopDataStack();
				r1 = PopDataStack();
				cobInst->AttachUnit(r2, r1);
			} break;
			case DROP: {
				r1 = PopDataStack();
				cobInst->DropUnit(r1);
			} break;

			// like bitwise ops, but only on values 1 and 0
			case LOGICAL_NOT: {
				r1 = PopDataStack();
				PushDataStack(int(r1 == 0));
			} break;
			case LOGICAL_AND: {
				r1 = PopDataStack();
				r2 = PopDataStack();
				PushDataStack(int(r1 && r2));
			} break;
			case LOGICAL_OR: {
				r1 = PopDataStack();
				r2 = PopDataStack();
				PushDataStack(int(r1 || r2));
			} break;
			case LOGICAL_XOR: {
				r1 = PopDataStack();
				r2 = PopDataStack();
				PushDataStack(int((!!r1) ^ (!!r2)));
			} break;


			case HIDE: {
				r1 = GET_LONG_PC();
				cobInst->SetVisibility(r1, false);
			} break;

			case SHOW: {
				r1 = GET_LONG_PC();

				int i;
				for (i = 0; i < MAX_WEAPONS_PER_UNIT; ++i)
					if (LocalFunctionID() == cobFile->scriptIndex[COBFN_FirePrimary + COBFN_Weapon_Funcs * i])
						break;

				// if true, we are in a Fire-script and should show a special flare effect
				if (i < MAX_WEAPONS_PER_UNIT) {
					cobInst->ShowFlare(r1);
				} else {
					cobInst->SetVisibility(r1, true);
				}
			} break;

			default: {
				const char* name = cobFile->name.c_str();
				const char* func = cobFile->scriptNames[LocalFunctionID()].c_str();

				LOG_L(L_ERROR, "[COBThread::%s] unknown opcode %x (in %s:%s at %x)", __func__, opcode, name, func, pc - 1);

				#if 0
				auto ei = execTrace.begin();
				while (ei != execTrace.end()) {
					LOG_L(L_ERROR, "\tprogctr: %3x  opcode: %s", __func__, *ei, GetOpcodeName(cobFile->code[*ei]));
					++ei;
				}
				#endif

				state = Dead;
				return false;
			} break;
		}
	}

	// can arrive here as dead, through CCobInstance::Signal()
	return (state != Dead);
}

void CCobThread::ShowError(const char* msg)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if ((errorCounter = std::max(errorCounter - 1, 0)) == 0)
		return;

	if (callStack.size() == 0) {
		LOG_L(L_ERROR, "[COBThread::%s] %s outside script execution (?)", __func__, msg);
		return;
	}

	const char* name = cobFile->name.c_str();
	const char* func = cobFile->scriptNames[LocalFunctionID()].c_str();

	LOG_L(L_ERROR, "[COBThread::%s] %s (in %s:%s at %x)", __func__, msg, name, func, pc - 1);
}


void CCobThread::LuaCall()
{
	RECOIL_DETAILED_TRACY_ZONE;
	const int r1 = GET_LONG_PC(); // script id
	const int r2 = GET_LONG_PC(); // arg count

	// setup the parameter array
	const int size = static_cast<int>(dataStack.size());
	const int argCount = std::min(r2, MAX_LUA_COB_ARGS);
	const int start = std::max(0, size - r2);
	const int end = std::min(size, start + argCount);

	for (int a = 0, i = start; i < end; i++) {
		luaArgs[a++] = dataStack[i];
	}

	if (r2 >= size) {
		dataStack.clear();
	} else {
		dataStack.resize(size - r2);
	}

	if (!luaRules) {
		luaArgs[0] = 0; // failure
		return;
	}

	// check script index validity
	if (static_cast<size_t>(r1) >= cobFile->luaScripts.size()) {
		luaArgs[0] = 0; // failure
		return;
	}

	int argsCount = argCount;
	luaRules->Cob2Lua(cobFile->luaScripts[r1], cobInst->GetUnit(), argsCount, luaArgs);
	retCode = luaArgs[0];
}


void CCobThread::AnimFinished(CUnitScript::AnimType type, int piece, int axis)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (piece != waitPiece || axis != waitAxis)
		return;

	if (!Reschedule(type))
		return;

	state = Run;
	waitPiece = -1;
	waitAxis = -1;

	cobEngine->ScheduleThread(this);
}

