#include "StreamBuffer.h"

#include "VBO.h"

#include "System/ContainerUtil.h"
#include "System/Log/ILog.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/GlobalRenderingInfo.h"

#include "System/Misc/TracyDefs.h"


//////////////////////////////////////////////////////////////////////


// To make sure that you don't stomp
// all over data that hasn't been used yet, you can insert a fence right after the
// last command that might read from a buffer, and then issue a call to
// glClientWaitSync() right before you write into the buffer.
void IStreamBufferConcept::PutBufferLocks()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (lockList.empty())
		return;

	spring::VectorSortUnique(lockList);

	for (auto& so : lockList) {
		if (glIsSync(*so))
			glDeleteSync(*so);

		*so = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	}
	lockList.clear();
}

IStreamBufferConcept::IStreamBufferConcept(StreamBufferCreationParams p, std::string_view bufferTypeName)
	: name{ p.name }
	, target{ p.target }
	, id{ 0 }
	, numElements { p.numElems }
	, byteSize{ 0 }
	, allocIdx{ 0 }
	, mapElemOffet{ 0 }
	, mapElemCount{ 0 }
	, optimizeForStreaming{ p.optimizeForStreaming }
{
	if (reportType)
		LOG_L(L_INFO, "[StreamBuffer::%s] Created StreamBuffer name %s type %s", __func__, name.c_str(), bufferTypeName.data());
}

void IStreamBufferConcept::QueueLockBuffer(GLsync& syncObj) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	lockList.emplace_back(&syncObj);
}

void IStreamBufferConcept::WaitBuffer(GLsync& syncObj) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!glIsSync(syncObj))
		return;

	uint32_t gWaitCount = 0;
	while (true) {
		GLenum waitReturn = glClientWaitSync(syncObj, GL_SYNC_FLUSH_COMMANDS_BIT, 1);
		if (waitReturn == GL_ALREADY_SIGNALED || waitReturn == GL_CONDITION_SATISFIED)
			break;

		gWaitCount++;
	}
	glDeleteSync(syncObj);
	syncObj = {};

	if (gWaitCount > 0)
		LOG_L(L_DEBUG, "[IStreamBuffer::WaitBuffer] Detected non-zero (%u) wait spins on stream buffer (%u, %s). Consider increasing numBuffers", gWaitCount, id, name.c_str());
}

void IStreamBufferConcept::CreateBuffer(uint32_t byteBufferSize, uint32_t newUsage)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (id == 0)
		glGenBuffers(1, &id);

	Bind();
	glBufferData(target, byteBufferSize, nullptr, newUsage);
	Unbind();

	assert(glIsBuffer(id));
}

void IStreamBufferConcept::CreateBufferStorage(uint32_t byteBufferSize, uint32_t flags)
{
	RECOIL_DETAILED_TRACY_ZONE;
	glGenBuffers(1, &id);

	Bind();
	glBufferStorage(target, byteBufferSize, nullptr, flags);
	Unbind();

	assert(glIsBuffer(id));
}

void IStreamBufferConcept::DeleteBuffer()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (glIsBuffer(id))
		glDeleteBuffers(1, &id);

	id = 0;
}

uint32_t IStreamBufferConcept::GetAlignedByteSize(uint32_t byteSizeRaw)
{
	RECOIL_DETAILED_TRACY_ZONE;
	return VBO::GetAlignedSize(target, byteSizeRaw);
}

void IStreamBufferConcept::Bind(uint32_t bindTarget) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	glBindBuffer(bindTarget > 0 ? bindTarget : target, id);
}

void  IStreamBufferConcept::Unbind(uint32_t bindTarget) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	glBindBuffer(bindTarget > 0 ? bindTarget : target, 0);
}

void IStreamBufferConcept::BindBufferRange(GLuint index, uint32_t bindTarget) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	glBindBufferRange(bindTarget > 0 ? bindTarget : this->target, index, id, allocIdx * this->byteSize, this->byteSize);
}

void IStreamBufferConcept::UnbindBufferRange(GLuint index, uint32_t bindTarget) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	glBindBufferRange(bindTarget > 0 ? bindTarget : this->target, index, 0u, allocIdx * this->byteSize, this->byteSize);
}