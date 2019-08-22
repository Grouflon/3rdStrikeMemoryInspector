#pragma once

template <typename T>
T Max(T _A, T _B) { return _A > _B ? _A : _B; }

template <typename T>
T Min(T _A, T _B) { return _A < _B ? _A : _B; }

template <typename T>
T Clamp(T _value, T _A, T _B) { return _A < _B ? Min(Max(_value, _A), _B) : Min(Max(_value, _B), _A); }
