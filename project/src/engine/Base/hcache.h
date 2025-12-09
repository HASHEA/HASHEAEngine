#pragma once
#include <chrono>
#include <optional>
#include <unordered_map>
#include <list>
#include <mutex>
#include <shared_mutex>
namespace AshEngine
{
	template <typename Key, typename Value, size_t Capacity>
	class LRUCache
	{
	public:
		using Clock = std::chrono::steady_clock;
		using TimePoint = Clock::time_point;
	private:
		struct CacheEntry
		{
			Value value;
			TimePoint access_time;
			typename std::list<Key>::iterator iter;
		};
		std::list<Key> lru_list;
		std::unordered_map<Key, CacheEntry> m_mapKV;
		mutable std::shared_mutex mutex_;
	public:
		std::optional<Value> get(const Key& key)
		{
			std::shared_lock lock(mutex_);

			auto it = m_mapKV.find(key);
			if (it == m_mapKV.end()) {
				return std::nullopt;
			}
			lru_list.splice(lru_list.begin(), lru_list, it->second.iter);
			it->second.access_time = Clock::now();
			return it->second.value;
		}
		void emplace(const Key& key, const Value& value)
		{
			std::unique_lock lock(mutex_);
			auto it = m_mapKV.find(key);
			if (it != m_mapKV.end())
			{
				it->second.value = value;
				it->second.access_time = Clock::now();
				lru_list.splice(lru_list.begin(), lru_list, it->second.iter);
				return;
			}
			if (m_mapKV.size() >= Capacity)
			{
				auto& last = lru_list.back();
				m_mapKV.erase(last);
				lru_list.pop_back();
			}
			lru_list.push_front(key);
			m_mapKV.emplace(key, CacheEntry{value,Clock::now(),lru_list.begin()});
		}
		void erase(const Key& key)
		{
			std::unique_lock lock(mutex_);
			auto it = m_mapKV.find(key);
			if (it != m_mapKV.end())
			{
				lru_list.erase(it->second.iter);
				m_mapKV.erase(it);
			}
		}
		void clear()
		{
			std::unique_lock lock(mutex_);
			lru_list.clear();
			m_mapKV.clear();
		}
		size_t size() const
		{
			std::unique_lock lock(mutex_);
			return m_mapKV.size();
		}

	};
};

