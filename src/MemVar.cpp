#include "MemVar.h"

#include <cstdlib>

MemVar::MemVar(const char* _name, size_t* _addresses, size_t _addressCount)
{
	m_name = _name;
	m_addresses = (size_t*)malloc(sizeof(*m_addresses) * _addressCount);
	//memcpy();
}

MemVar::~MemVar()
{

}

U8MemVar::U8MemVar(const char* _name, size_t* _addresses, size_t _addressCount)
	:MemVar(_name, _addresses, _addressCount)
{

}

U8MemVar::~U8MemVar()
{

}

void U8MemVar::read(HANDLE _handle, size_t _baseOffset)
{

}

MemVarType U8MemVar::getType() const
{
	return MemVarType_U8;
}

uint8_t U8MemVar::getValue() const
{
	return m_value;
}
void U8MemVar::write(uint8_t _value)
{

}