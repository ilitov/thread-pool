#ifndef _FUNCTION_WRAPPER_IMPLEMENTATION_HEADER_
#define _FUNCTION_WRAPPER_IMPLEMENTATION_HEADER_

#include <memory> // Unique pointer.

class FunctionWrapper {
	class FunctionWrapperBase {
	public:
		virtual void execute() = 0;
		virtual ~FunctionWrapperBase() = default;
	};

	template <typename Func>
	class FunctionWrapperImpl : public FunctionWrapperBase {
	public:
		FunctionWrapperImpl(Func &&func) : m_func(std::move(func)) {}
		FunctionWrapperImpl(const FunctionWrapperImpl &r) = delete;
		FunctionWrapperImpl& operator=(const FunctionWrapperImpl &rhs) = delete;
		virtual ~FunctionWrapperImpl() = default;

		virtual void execute() override {
			m_func();
		}

	private:
		Func m_func;
	};

public:
	template <typename Func>
	explicit FunctionWrapper(Func &&func) 
		: m_funcWImpl(new FunctionWrapperImpl<Func>(std::move(func))) {

	}
	FunctionWrapper() = default;
	FunctionWrapper(const FunctionWrapper &r) = delete;
	FunctionWrapper& operator=(const FunctionWrapper &rhs) = delete;
	FunctionWrapper(FunctionWrapper &&r) : m_funcWImpl(std::move(r.m_funcWImpl)) {}
	FunctionWrapper& operator=(FunctionWrapper &&rhs) {
		if (this != &rhs) {
			m_funcWImpl = std::move(rhs.m_funcWImpl);
		}

		return *this;
	}
	~FunctionWrapper() = default;

public:
	void operator()() {
		if (m_funcWImpl) {
			m_funcWImpl->execute();
		}
	}

private:
	std::unique_ptr<FunctionWrapperBase> m_funcWImpl;
};

#endif // !_FUNCTION_WRAPPER_IMPLEMENTATION_HEADER_
