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
	virtual operator bool() = 0;
	virtual void enable() = 0;
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

	virtual ~TaskEnabler()
	{ }

	virtual operator bool()
	{
		return m_value;
	}

	virtual void enable()
	{
		m_value = true;
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
		, m_enabled(false)
	{
	}

	TaskEnabler(std::array<std::shared_ptr<ITaskEnabler>, N>&& deps)
		: m_deps(std::forward<std::array<std::shared_ptr<ITaskEnabler>, N>>(deps))
		, m_enabled(false)
	{
	}

	TaskEnabler(std::initializer_list<std::shared_ptr<ITaskEnabler>> deps)
		: m_enabled(false)
	{
		std::copy(deps.begin(), deps.end(), m_deps.begin());
	}

	virtual ~TaskEnabler()
	{ }

	virtual operator bool()
	{
		if (m_enabled)
			return true;

		bool result(true);
		
		for (auto& n : m_deps)
			result &= *n;

		if (result)
			m_enabled = true;
		
		return m_enabled;
	}

	virtual void enable()
	{
		if (m_enabled)
			return;

		for (auto& n : m_deps)
			n->enable();

		m_enabled = true;
	}

private:

	std::array<std::shared_ptr<ITaskEnabler>, N> m_deps;
	bool m_enabled;
};

class DynamicTaskEnabler : public ITaskEnabler
{
public:

	DynamicTaskEnabler(const std::vector<std::shared_ptr<ITaskEnabler>>& deps)
		: m_deps(deps)
		, m_enabled(false)
	{ }

	DynamicTaskEnabler(std::vector<std::shared_ptr<ITaskEnabler>>&& deps)
		: m_deps(std::forward<std::vector<std::shared_ptr<ITaskEnabler>>>(deps))
		, m_enabled(false)
	{ }

	virtual ~DynamicTaskEnabler()
	{ }

	virtual operator bool()
	{
		if (m_enabled)
			return true;

		bool result(true);

		for (auto& n : m_deps)
			result &= *n;

		if (result)
			m_enabled = true;

		return m_enabled;
	}

	virtual void enable()
	{
		if (m_enabled)
			return;

		for (auto& n : m_deps)
			n->enable();

		m_enabled = true;
	}

private:

	std::vector<std::shared_ptr<ITaskEnabler>> m_deps;
	bool m_enabled;
};

} // namespace conc11
