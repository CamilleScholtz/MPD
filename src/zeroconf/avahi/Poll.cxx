/*
 * Copyright 2003-2021 The Music Player Daemon Project
 * http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "Poll.hxx"
#include "event/SocketEvent.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "time/Convert.hxx"

static unsigned
FromAvahiWatchEvent(AvahiWatchEvent e) noexcept
{
	return (e & AVAHI_WATCH_IN ? SocketEvent::READ : 0) |
		(e & AVAHI_WATCH_OUT ? SocketEvent::WRITE : 0);
}

static AvahiWatchEvent
ToAvahiWatchEvent(unsigned e) noexcept
{
	return AvahiWatchEvent((e & SocketEvent::READ ? AVAHI_WATCH_IN : 0) |
			       (e & SocketEvent::WRITE ? AVAHI_WATCH_OUT : 0) |
			       (e & SocketEvent::ERROR ? AVAHI_WATCH_ERR : 0) |
			       (e & SocketEvent::HANGUP ? AVAHI_WATCH_HUP : 0));
}

struct AvahiWatch final {
	SocketEvent event;

	const AvahiWatchCallback callback;
	void *const userdata;

	AvahiWatchEvent received;

public:
	AvahiWatch(SocketDescriptor _fd, AvahiWatchEvent _event,
		   AvahiWatchCallback _callback, void *_userdata,
		   EventLoop &_loop) noexcept
		:event(_loop, BIND_THIS_METHOD(OnSocketReady), _fd),
		 callback(_callback), userdata(_userdata),
		 received(AvahiWatchEvent(0)) {
		event.Schedule(FromAvahiWatchEvent(_event));
	}

	static void WatchUpdate(AvahiWatch *w,
				AvahiWatchEvent event) noexcept {
		w->event.Schedule(FromAvahiWatchEvent(event));
	}

	static AvahiWatchEvent WatchGetEvents(AvahiWatch *w) noexcept {
		return w->received;
	}

	static void WatchFree(AvahiWatch *w) noexcept {
		delete w;
	}

private:
	void OnSocketReady(unsigned flags) noexcept {
		received = ToAvahiWatchEvent(flags);
		callback(this, event.GetSocket().Get(), received, userdata);
		received = AvahiWatchEvent(0);
	}
};

struct AvahiTimeout final {
	CoarseTimerEvent timer;

	const AvahiTimeoutCallback callback;
	void *const userdata;

public:
	AvahiTimeout(const struct timeval *tv,
		     AvahiTimeoutCallback _callback, void *_userdata,
		     EventLoop &_loop) noexcept
		:timer(_loop, BIND_THIS_METHOD(OnTimeout)),
		 callback(_callback), userdata(_userdata) {
		if (tv != nullptr)
			timer.Schedule(ToSteadyClockDuration(*tv));
	}

	static void TimeoutUpdate(AvahiTimeout *t,
				  const struct timeval *tv) noexcept {
		if (tv != nullptr)
			t->timer.Schedule(ToSteadyClockDuration(*tv));
		else
			t->timer.Cancel();
	}

	static void TimeoutFree(AvahiTimeout *t) noexcept {
		delete t;
	}

private:
	void OnTimeout() noexcept {
		callback(this, userdata);
	}
};

MyAvahiPoll::MyAvahiPoll(EventLoop &_loop) noexcept
	:event_loop(_loop)
{
	watch_new = WatchNew;
	watch_update = AvahiWatch::WatchUpdate;
	watch_get_events = AvahiWatch::WatchGetEvents;
	watch_free = AvahiWatch::WatchFree;
	timeout_new = TimeoutNew;
	timeout_update = AvahiTimeout::TimeoutUpdate;
	timeout_free = AvahiTimeout::TimeoutFree;
}

AvahiWatch *
MyAvahiPoll::WatchNew(const AvahiPoll *api, int fd, AvahiWatchEvent event,
		      AvahiWatchCallback callback, void *userdata) noexcept
{
	const MyAvahiPoll &poll = *(const MyAvahiPoll *)api;

	return new AvahiWatch(SocketDescriptor(fd), event, callback, userdata,
			      poll.event_loop);
}

AvahiTimeout *
MyAvahiPoll::TimeoutNew(const AvahiPoll *api, const struct timeval *tv,
			AvahiTimeoutCallback callback, void *userdata) noexcept
{
	const MyAvahiPoll &poll = *(const MyAvahiPoll *)api;

	return new AvahiTimeout(tv, callback, userdata,
				poll.event_loop);
}
