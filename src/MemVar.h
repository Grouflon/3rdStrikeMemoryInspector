#pragma once

#include <cstdint>

typedef void* HANDLE;

enum MemVarType
{
	MemVarType_U8,
	MemVarType_S8,
	MemVarType_U16,
	MemVarType_S16,
	MemVarType_U32,
	MemVarType_S32,
	MemVarType_U64,
	MemVarType_S64
};

class MemVar
{
public:
	MemVar(const char* _name, size_t* _addresses, size_t _addressCount);
	virtual ~MemVar();

	virtual void read(HANDLE _handle, size_t _baseOffset) = 0;
	virtual MemVarType getType() const = 0;

	const char* getName() const { return m_name; }

private:
	const char* m_name = "";
	size_t* m_addresses = nullptr;
	size_t m_addressCount = 0;
};


class U8MemVar : MemVar
{
public:
	U8MemVar(const char* _name, size_t* _addresses, size_t _addressCount);
	virtual ~U8MemVar();

	virtual void read(HANDLE _handle, size_t _baseOffset) override;
	virtual MemVarType getType() const override;

	uint8_t getValue() const;
	void write(uint8_t _value);

private:
	uint8_t m_value;
};