// ================================================ //
// File: GUI.hpp
// Author: Jordan Sparks
// COSC 4327 Operating Systems Lab 2, Dr. Burris
// ================================================ //
// Defines GUI helper functions.
// ================================================ //

#ifndef __GUI_HPP__
#define __GUI_HPP__

// ================================================ //

#include "stdafx.hpp"

// ================================================ //

// Inserts a new item into listview.
const int InsertListviewItem(const HWND hList, const int index, 
					   const std::string& text);

// Sets a subitem of a listview item.
const BOOL SetListviewItem(const HWND hList, const int index, const int subitem,
					 const std::string& text);

// Returns the index of the item containing the specified text at the 
// specified sub item. Returns -1 if not found.
const int GetListviewItemIndex(const HWND hList, const int subitem, 
							   const std::string& text);

// ================================================ //

#endif

// ================================================ //