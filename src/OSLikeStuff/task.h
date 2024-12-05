/*
 * Copyright © 2024 Mark Adams
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef DELUGE_TASK_H
#define DELUGE_TASK_H
#include "task_scheduler.h"
// internal to the scheduler - do not include from anywhere else
struct StatBlock {

	Time min{std::numeric_limits<Time>::infinity()};
	Time max{std::numeric_limits<Time>::infinity() * -1};

	/// Running average, computed as (last + avg) / 2
	Time average{0};

	[[gnu::hot]] void update(Time v) {

		min = std::min(v, min);
		max = std::max(v, max);
		average = average.average(v);
		average = std::max(Time(1), average);
	}
	void reset() {

		min = std::numeric_limits<Time>::infinity();
		max = std::numeric_limits<Time>::infinity() * -1;

		average = 0;
	}
};

struct TaskSchedule {
	// 0 is highest priority
	uint8_t priority;
	// time to wait between return and calling the function again
	Time backOffPeriod;
	// target time between function calls
	Time targetInterval;
	// maximum time between function calls
	Time maxInterval;
};

// currently 14 are in use
constexpr int kMaxTasks = 25;
struct Task {
	Task() = default;

	// constructor for making a "once" task
	Task(TaskHandle _handle, uint8_t _priority, Time timeNow, Time _timeToWait, const char* _name) {
		handle = _handle;
		lastCallTime = timeNow;
		schedule = TaskSchedule{_priority, _timeToWait, _timeToWait, _timeToWait * 2};
		name = _name;
		removeAfterUse = true;
	}
	// makes a repeating task
	Task(TaskHandle task, TaskSchedule _schedule, const char* _name) {
		handle = task;
		schedule = _schedule;
		name = _name;
		removeAfterUse = false;
	}
	// makes a conditional task
	Task(TaskHandle task, uint8_t priority, RunCondition _condition, const char* _name) {
		handle = task;
		// good to go as soon as it's marked as runnable
		schedule = {priority, 0, 0, 0};
		name = _name;
		removeAfterUse = true;
		runnable = false;
		condition = _condition;
	}

	void updateNextTimes(Time startTime, Time runtime) {

		lastCallTime = startTime;

		durationStats.update(runtime);
		totalTime += runtime;
		lastRunTime = runtime;
		timesCalled += 1;

		idealCallTime = startTime + schedule.targetInterval - durationStats.average;
		latestCallTime = startTime + schedule.maxInterval - durationStats.average;
	}
	TaskHandle handle{nullptr};
	TaskSchedule schedule{0, 0, 0, 0};
	Time idealCallTime{0};
	Time latestCallTime{0};
	Time lastCallTime{0};
	Time lastFinishTime{0};

	StatBlock durationStats;
#if SCHEDULER_DETAILED_STATS
	StatBlock latency;
#endif
	bool runnable{true};
	RunCondition condition{nullptr};
	bool removeAfterUse{false};
	const char* name{nullptr};

	Time totalTime{0};
	int32_t timesCalled{0};
	Time lastRunTime;
};

#endif // DELUGE_TASK_H
