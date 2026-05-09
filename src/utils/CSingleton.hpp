#pragma once

#include <mutex>

template<class T>
class CSingleton
{
protected:
	static T* m_Instance;

public:
	CSingleton()
	{ }
	virtual ~CSingleton()
	{ }

	inline static T* Get()
	{
		static std::once_flag s_flag;
		std::call_once(s_flag, []() { m_Instance = new T; });
		return m_Instance;
	}

	inline static void Destroy()
	{
		delete m_Instance;
		m_Instance = nullptr;
	}
};

template <class T>
T* CSingleton<T>::m_Instance = nullptr;