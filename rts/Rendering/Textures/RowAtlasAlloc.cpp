/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "RowAtlasAlloc.h"

#include <algorithm>
#include <vector>
#include <set>
#include <bit>

#include "System/Misc/TracyDefs.h"

inline bool CRowAtlasAlloc::CompareTex(const SAtlasEntry* tex1, const SAtlasEntry* tex2)
{
	RECOIL_DETAILED_TRACY_ZONE;
	// sort by large to small

	if (tex1->size.y > tex2->size.y) return true;
	if (tex2->size.y > tex1->size.y) return false;

	if (tex1->size.x > tex2->size.x) return true;
	if (tex2->size.x > tex1->size.x) return false;

	// silly but will help stabilizing the placement on reload
	if (tex1->name > tex2->name) return true;
	if (tex2->name > tex1->name) return false;

	return false;
}


void CRowAtlasAlloc::EstimateNeededSize()
{
	RECOIL_DETAILED_TRACY_ZONE;
	int spaceNeeded = 0;
	int spaceFree = atlasSize.x * (atlasSize.y - nextRowPos);

	for (const auto& it: entries) {
		spaceNeeded += it.second.size.x * it.second.size.y;
	}
	for (auto& row: imageRows) {
		spaceFree += row.height * (atlasSize.x - row.width);
	}

	while (spaceFree < spaceNeeded * 1.2f) {
		if (atlasSize.x >= maxsize.x && atlasSize.y >= maxsize.y)
			break;

		// Resize texture
		if ((atlasSize.x << 1) <= maxsize.x) {
			spaceFree += atlasSize.x * atlasSize.y;
			atlasSize.x = std::min(maxsize.x, atlasSize.x << 1);
		}
		if ((atlasSize.y << 1) <= maxsize.y) {
			spaceFree += atlasSize.x * atlasSize.y;
			atlasSize.y = std::min(maxsize.y, atlasSize.y << 1);
		}
	}
}


CRowAtlasAlloc::Row* CRowAtlasAlloc::AddRow(int glyphWidth, int glyphHeight)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const int wantedRowHeight = glyphHeight;

	while (atlasSize.y < (nextRowPos + wantedRowHeight)) {
		if (atlasSize.x >= maxsize.x && atlasSize.y >= maxsize.y) {
			//throw texture_size_exception();
			return nullptr;
		}

		// Resize texture
		atlasSize.x = std::min(maxsize.x, atlasSize.x << 1);
		atlasSize.y = std::min(maxsize.y, atlasSize.y << 1);
	}

	imageRows.emplace_back(nextRowPos, wantedRowHeight);
	nextRowPos += wantedRowHeight;
	// safe, is never leaked
	return &imageRows.back();
}


bool CRowAtlasAlloc::Allocate()
{
	RECOIL_DETAILED_TRACY_ZONE;
	bool success = true;

	// revert the used height clamping at the bottom of this function
	// else for the case when Allocate() is called multiple times, the
	// width would grow faster than height
	// also AddRow() only works with PowerOfTwo values.
	atlasSize.y = std::bit_ceil <uint32_t>(atlasSize.y);

	// it gives much better results when we resize the available space before starting allocation
	// esp. allocation is more horizontal and so we can clip more free space at bottom
	EstimateNeededSize();

	// sort new entries by height from large to small
	std::vector<SAtlasEntry*> memtextures;
	memtextures.reserve(entries.size());

	std::set<std::string> sortedNames;
	for (auto& entry : entries) {
		sortedNames.insert(entry.first);
	}

	for (auto& name : sortedNames) {
		memtextures.push_back(&entries[name]);
	}
	std::stable_sort(memtextures.begin(), memtextures.end(), CRowAtlasAlloc::CompareTex);

	int padding = 1 << GetNumTexLevels();

	// find space for them
	for (auto& curtex: memtextures) {
		Row* row = FindRow(curtex->size.x + padding, curtex->size.y + padding);

		if (row == nullptr) {
			success = false;
			continue;
		}

		curtex->texCoords.x1 = row->width;
		curtex->texCoords.y1 = row->position;
		curtex->texCoords.x2 = row->width + curtex->size.x;
		curtex->texCoords.y2 = row->position + curtex->size.y;

		row->width += (curtex->size.x + padding);
	}

	atlasSize.y = nextRowPos;

	return success;
}

int CRowAtlasAlloc::GetNumTexLevels() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	return std::min(
		static_cast<int>(std::bit_width(static_cast<uint32_t>(GetMinDim()))),
		numLevels
	);
}


CRowAtlasAlloc::Row* CRowAtlasAlloc::FindRow(int glyphWidth, int glyphHeight)
{
	RECOIL_DETAILED_TRACY_ZONE;
	int   bestWidth = atlasSize.x;
	float bestRatio = 10000.0f;
	Row*  bestRow   = nullptr;

	// first try to find a row with similar height
	for(auto& row: imageRows) {
		// Check if there is enough space in this row
		if (glyphWidth > (atlasSize.x - row.width))
			continue;
		if (glyphHeight > row.height)
			continue;

		const float ratio = float(row.height) / glyphHeight;
		if ((ratio < bestRatio) || ((ratio == bestRatio) && (row.width < bestWidth))) {
			bestWidth = row.width;
			bestRatio = ratio;
			bestRow   = &row;
		}
	}

	if (bestRow == nullptr)
		bestRow = AddRow(glyphWidth, glyphHeight);

	return bestRow;
}
