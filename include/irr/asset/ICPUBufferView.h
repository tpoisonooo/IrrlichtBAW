#ifndef __IRR_I_CPU_BUFFER_VIEW_H_INCLUDED__
#define __IRR_I_CPU_BUFFER_VIEW_H_INCLUDED__


#include <utility>

#include "irr/asset/IAsset.h"
#include "irr/asset/IBufferView.h"
#include "irr/asset/ICPUBuffer.h"

namespace irr
{
namespace asset
{

class ICPUBufferView : public IBufferView<ICPUBuffer>, public IAsset
{
	public:
		ICPUBufferView(core::smart_refctd_ptr<ICPUBuffer> _buffer, E_FORMAT _format, size_t _offset = 0ull, size_t _size = ICPUBufferView::whole_buffer) :
			IBufferView<ICPUBuffer>(std::move(_buffer), _format, _offset, _size)
		{}

		size_t conservativeSizeEstimate() const override { return m_size; }
		void convertToDummyObject() override { }
		E_TYPE getAssetType() const override { return ET_BUFFER_VIEW; }

		ICPUBuffer* getUnderlyingBuffer() { return m_buffer.get(); }
		const ICPUBuffer* getUnderlyingBuffer() const { return m_buffer.get(); }

		inline void setOffsetInBuffer(size_t _offset) { m_offset = _offset; }
		inline void setSize(size_t _size) { m_size = _size; }

	protected:
		virtual ~ICPUBufferView() = default;
};

}
}

#endif