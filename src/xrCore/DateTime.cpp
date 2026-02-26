#include "stdafx.h"
#include "DateTime.hpp"

Time::Time()
{
	t = time(nullptr);
	aTm = *localtime(&t);
}

int Time::GetSeconds() const
{
	return aTm.tm_sec;
}

int Time::GetMinutes() const
{
	return aTm.tm_min;
}

int Time::GetHours() const
{
	return aTm.tm_hour;
}

int Time::GetDay() const
{
	return aTm.tm_mday;
}

int Time::GetMonth() const
{
	return aTm.tm_mon + 1;
}

int Time::GetYear() const
{
	return aTm.tm_year + 1900;
}

Time::string Time::GetSecondsString() const
{
    string64 buf = { 0 };
    itoa(GetSeconds(), &buf[0], 10);

    return (GetSeconds() < 10) ? "0" + xr_string(buf) : xr_string(buf);
}

Time::string Time::GetMinutesString() const
{
    string64 buf = { 0 };
    itoa(GetMinutes(), &buf[0], 10);

    return (GetMinutes() < 10) ? "0" + xr_string(buf) : xr_string(buf);
}

Time::string Time::GetHoursString() const
{
    string64 buf = { 0 };
    itoa(GetHours(), &buf[0], 10);

    return (GetHours() < 10) ? "0" + xr_string(buf) : xr_string(buf);
}

Time::string Time::GetDayString() const
{
    string64 buf = { 0 };
    itoa(GetDay(), &buf[0], 10);

    return (GetDay() < 10) ? "0" + xr_string(buf) : xr_string(buf);
}

Time::string Time::GetMonthString() const
{
    string64 buf = { 0 };
    itoa(GetMonth(), &buf[0], 10);

    return (GetMonth() < 10) ? "0" + xr_string(buf) : xr_string(buf);
}

Time::string Time::GetYearString() const
{
    string64 buf = { 0 };
    itoa(GetYear(), &buf[0], 10);

    return xr_string(buf);
}
