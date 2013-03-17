#pragma once

#include <array>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

namespace conc11
{

struct ITaskEnabler 
{
	virtual operator bool() const = 0;
	virtual bool enable() = 0;
};

template<typename T, unsigned int N = 1>
class TaskEnabler;

template<>
class TaskEnabler<bool, 1> : public ITaskEnabler
{
public:

	TaskEnabler(bool value = false)
		: m_value(value)
	{ }

	virtual operator bool() const
	{
		return m_value;
	}

	virtual bool enable()
	{
		m_value = true;

		return m_value;
	}

private:

	bool m_value;
};

template<unsigned int N>
class TaskEnabler<std::shared_ptr<ITaskEnabler>, N> : public ITaskEnabler
{
public:

	TaskEnabler(const std::array<std::shared_ptr<ITaskEnabler>, N>& deps)
		: m_deps(deps)
	{
	}

	TaskEnabler(std::array<std::shared_ptr<ITaskEnabler>, N>&& deps)
		: m_deps(std::forward<std::array<std::shared_ptr<ITaskEnabler>, N>>(deps))
	{
	}

	TaskEnabler(std::initializer_list<std::shared_ptr<ITaskEnabler>> deps)
	{
		std::copy(deps.begin(), deps.end(), m_deps.begin());
	}

	virtual operator bool() const
	{
		bool result(true);
		
		for (auto& n : m_deps)
			result &= *n;
		
		return result;
	}

	virtual bool enable()
	{
		return false;
	}

private:

	std::array<std::shared_ptr<ITaskEnabler>, N> m_deps;
};

class DynamicTaskEnabler : public ITaskEnabler
{
public:

	DynamicTaskEnabler(const std::vector<std::shared_ptr<ITaskEnabler>>& deps)
		: m_deps(deps)
	{ }

	DynamicTaskEnabler(std::vector<std::shared_ptr<ITaskEnabler>>&& deps)
		: m_deps(std::forward<std::vector<std::shared_ptr<ITaskEnabler>>>(deps))
	{ }

	virtual operator bool() const
	{
		bool result(true);

		for (auto& n : m_deps)
			result &= (*n);

		return result;
	}

	virtual bool enable()
	{
		return false;
	}

private:

	std::vector<std::shared_ptr<ITaskEnabler>> m_deps;
};

} // namespace conc11
