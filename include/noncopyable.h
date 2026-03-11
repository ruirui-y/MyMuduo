#pragma once

class noncopyable
{
public:
	// É¾³ý¿½±´ºÍ¸³Öµ

	/*
	* noncopyable cb;
	* noncopyable cb1;
	* cb1 = cb;  <==> cb1.operator=(cb);
	*/

	noncopyable(const noncopyable&) = delete;
	noncopyable& operator=(const noncopyable&) = delete;

protected:
    noncopyable() = default;
	~noncopyable() = default;
};