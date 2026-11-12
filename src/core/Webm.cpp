// SPDX-License-Identifier: GPL-3.0-or-later
#include "Webm.h"

#include <algorithm>
#include <cstring>

namespace pp {

namespace {

// Matroska element ids, with their length marker bits.
enum : uint32_t {
	kEbml = 0x1A45DFA3,
	kSegment = 0x18538067,
	kInfo = 0x1549A966,
	kTimecodeScale = 0x2AD7B1,
	kDuration = 0x4489,
	kTracks = 0x1654AE6B,
	kTrackEntry = 0xAE,
	kTrackNumber = 0xD7,
	kTrackType = 0x83,
	kCodecId = 0x86,
	kVideo = 0xE0,
	kPixelWidth = 0xB0,
	kPixelHeight = 0xBA,
	kAlphaMode = 0x53C0,
	kCluster = 0x1F43B675,
	kTimecode = 0xE7,
	kSimpleBlock = 0xA3,
	kBlockGroup = 0xA0,
	kBlock = 0xA1,
	kBlockAdditions = 0x75A1,
	kBlockMore = 0xA6,
	kBlockAddId = 0xEE,
	kBlockAdditional = 0xA5
};

const uint64_t kUnknownSize = ~(uint64_t)0;


class Reader {
public:
	Reader(const std::string& data, size_t start, size_t end)
		: fData((const uint8_t*)data.data()), fPos(start), fEnd(end) {}

	bool AtEnd() const { return fPos >= fEnd; }
	size_t Pos() const { return fPos; }
	size_t End() const { return fEnd; }

	// An element id (marker kept) and its data size; false at the end or
	// on a broken element.
	bool Element(uint32_t* id, uint64_t* size)
	{
		if (fPos >= fEnd)
			return false;
		uint8_t first = fData[fPos];
		int length = first >= 0x80 ? 1 : first >= 0x40 ? 2 : first >= 0x20 ? 3
			: first >= 0x10 ? 4 : 0;
		if (length == 0 || fPos + length > fEnd)
			return false;
		*id = 0;
		for (int i = 0; i < length; i++)
			*id = (*id << 8) | fData[fPos + i];
		fPos += length;
		return Number(size, true);
	}

	// A variable-length number; `sizeField`: all ones means "unknown".
	bool Number(uint64_t* value, bool sizeField = false)
	{
		if (fPos >= fEnd)
			return false;
		uint8_t first = fData[fPos];
		int length = 1;
		while (length <= 8 && !(first & (0x80 >> (length - 1))))
			length++;
		if (length > 8 || fPos + length > fEnd)
			return false;
		uint64_t v = first & (0xFF >> length);
		bool allOnes = v == (uint64_t)(0xFF >> length);
		for (int i = 1; i < length; i++) {
			v = (v << 8) | fData[fPos + i];
			allOnes = allOnes && fData[fPos + i] == 0xFF;
		}
		fPos += length;
		*value = sizeField && allOnes ? kUnknownSize : v;
		return true;
	}

	uint64_t Unsigned(uint64_t size)
	{
		uint64_t v = 0;
		for (uint64_t i = 0; i < size && fPos < fEnd; i++)
			v = (v << 8) | fData[fPos++];
		return v;
	}

	double Float(uint64_t size)
	{
		uint64_t bits = Unsigned(size);
		if (size == 4) {
			uint32_t b = (uint32_t)bits;
			float f;
			memcpy(&f, &b, 4);
			return f;
		}
		double d;
		memcpy(&d, &bits, 8);
		return size == 8 ? d : 0;
	}

	std::string Bytes(uint64_t size)
	{
		size_t n = (size_t)std::min<uint64_t>(size, fEnd - fPos);
		std::string out((const char*)fData + fPos, n);
		fPos += n;
		return out;
	}

	void Skip(uint64_t size)
	{
		fPos = size == kUnknownSize || size > fEnd - fPos ? fEnd : fPos + size;
	}

	// The end of an element of `size` starting here.
	size_t EndOf(uint64_t size) const
	{
		return size == kUnknownSize || size > fEnd - fPos ? fEnd : fPos + size;
	}

private:
	const uint8_t* fData;
	size_t fPos;
	size_t fEnd;
};


struct Parser {
	const std::string& data;
	WebmVideo* video;
	uint64_t track = 0;          // the video track's number
	uint64_t timecodeScale = 1000000;
	double durationTicks = 0;

	void Info(Reader r)
	{
		uint32_t id;
		uint64_t size;
		while (r.Element(&id, &size)) {
			if (id == kTimecodeScale)
				timecodeScale = r.Unsigned(size);
			else if (id == kDuration)
				durationTicks = r.Float(size);
			else
				r.Skip(size);
		}
	}

	void Tracks(Reader r)
	{
		uint32_t id;
		uint64_t size;
		while (r.Element(&id, &size)) {
			if (id != kTrackEntry) {
				r.Skip(size);
				continue;
			}
			Reader entry(data, r.Pos(), r.EndOf(size));
			r.Skip(size);
			uint64_t number = 0, type = 0, width = 0, height = 0, alpha = 0;
			std::string codec;
			uint32_t eid;
			uint64_t esize;
			while (entry.Element(&eid, &esize)) {
				if (eid == kTrackNumber)
					number = entry.Unsigned(esize);
				else if (eid == kTrackType)
					type = entry.Unsigned(esize);
				else if (eid == kCodecId)
					codec = entry.Bytes(esize);
				else if (eid == kVideo) {
					Reader v(data, entry.Pos(), entry.EndOf(esize));
					entry.Skip(esize);
					uint32_t vid;
					uint64_t vsize;
					while (v.Element(&vid, &vsize)) {
						if (vid == kPixelWidth)
							width = v.Unsigned(vsize);
						else if (vid == kPixelHeight)
							height = v.Unsigned(vsize);
						else if (vid == kAlphaMode)
							alpha = v.Unsigned(vsize);
						else
							v.Skip(vsize);
					}
				} else
					entry.Skip(esize);
			}
			if (type == 1 && track == 0) {
				track = number;
				video->codec = codec;
				video->width = (int32_t)width;
				video->height = (int32_t)height;
				video->alpha = alpha == 1;
			}
		}
	}

	// A Block or SimpleBlock's frame, if it's the video track's.
	bool Block(const std::string& block, int64_t clusterTime, bool simple, WebmFrame* frame)
	{
		Reader r(block, 0, block.size());
		uint64_t number;
		if (!r.Number(&number) || number != track || block.size() < r.Pos() + 3)
			return false;
		size_t at = r.Pos();
		int16_t relative = (int16_t)(((uint8_t)block[at] << 8) | (uint8_t)block[at + 1]);
		uint8_t flags = (uint8_t)block[at + 2];
		if (flags & 0x06)
			return false;   // laced
		frame->time = (int64_t)((clusterTime + relative) * timecodeScale / 1000000);
		frame->keyframe = simple && (flags & 0x80);
		frame->data = block.substr(at + 3);
		return true;
	}

	void BlockGroup(Reader r, int64_t clusterTime)
	{
		WebmFrame frame;
		bool found = false, reference = false;
		uint32_t id;
		uint64_t size;
		while (r.Element(&id, &size)) {
			if (id == kBlock)
				found = Block(r.Bytes(size), clusterTime, false, &frame);
			else if (id == 0xFB) {   // ReferenceBlock: not a keyframe
				reference = true;
				r.Skip(size);
			} else if (id == kBlockAdditions) {
				Reader adds(data, r.Pos(), r.EndOf(size));
				r.Skip(size);
				uint32_t aid;
				uint64_t asize;
				while (adds.Element(&aid, &asize)) {
					if (aid != kBlockMore) {
						adds.Skip(asize);
						continue;
					}
					Reader more(data, adds.Pos(), adds.EndOf(asize));
					adds.Skip(asize);
					uint64_t addId = 1;
					std::string additional;
					uint32_t mid;
					uint64_t msize;
					while (more.Element(&mid, &msize)) {
						if (mid == kBlockAddId)
							addId = more.Unsigned(msize);
						else if (mid == kBlockAdditional)
							additional = more.Bytes(msize);
						else
							more.Skip(msize);
					}
					if (addId == 1)
						frame.alpha = additional;
				}
			} else
				r.Skip(size);
		}
		if (found) {
			frame.keyframe = !reference;
			video->frames.push_back(frame);
		}
	}

	void Cluster(Reader r)
	{
		int64_t clusterTime = 0;
		uint32_t id;
		uint64_t size;
		while (r.Element(&id, &size)) {
			if (id == kTimecode)
				clusterTime = (int64_t)r.Unsigned(size);
			else if (id == kSimpleBlock) {
				WebmFrame frame;
				if (Block(r.Bytes(size), clusterTime, true, &frame))
					video->frames.push_back(frame);
			} else if (id == kBlockGroup) {
				BlockGroup(Reader(data, r.Pos(), r.EndOf(size)), clusterTime);
				r.Skip(size);
			} else if (id == kCluster) {
				// A cluster of unknown size ends where the next one starts.
				Cluster(Reader(data, r.Pos(), r.EndOf(size)));
				r.Skip(size);
			} else
				r.Skip(size);
		}
	}
};

} // namespace


bool
ParseWebm(const std::string& file, WebmVideo* video)
{
	*video = WebmVideo();
	Reader top(file, 0, file.size());
	uint32_t id;
	uint64_t size;
	if (!top.Element(&id, &size) || id != kEbml)
		return false;
	top.Skip(size);
	Parser parser{file, video};
	while (top.Element(&id, &size)) {
		if (id != kSegment) {
			top.Skip(size);
			continue;
		}
		Reader segment(file, top.Pos(), top.EndOf(size));
		top.Skip(size);
		uint32_t sid;
		uint64_t ssize;
		while (segment.Element(&sid, &ssize)) {
			Reader child(file, segment.Pos(), segment.EndOf(ssize));
			if (sid == kInfo)
				parser.Info(child);
			else if (sid == kTracks)
				parser.Tracks(child);
			else if (sid == kCluster && parser.track != 0)
				parser.Cluster(child);
			segment.Skip(ssize);
		}
	}
	video->duration = parser.durationTicks * parser.timecodeScale / 1000000;
	return parser.track != 0 && !video->frames.empty();
}

} // namespace pp
