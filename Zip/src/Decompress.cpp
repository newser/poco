//
// Decompress.cpp
//
// $Id: //poco/1.3/Zip/src/Decompress.cpp#4 $
//
// Library: Zip
// Package: Zip
// Module:  Decompress
//
// Copyright (c) 2007, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:
// 
// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//


#include "Poco/Zip/Decompress.h"
#include "Poco/Zip/ZipLocalFileHeader.h"
#include "Poco/Zip/ZipArchive.h"
#include "Poco/Zip/ZipStream.h"
#include "Poco/Zip/ZipException.h"
#include "Poco/File.h"
#include "Poco/Exception.h"
#include "Poco/StreamCopier.h"
#include "Poco/Delegate.h"
#include <fstream>


namespace Poco {
namespace Zip {


Decompress::Decompress(std::istream& in, const Poco::Path& outputDir, bool flattenDirs, bool keepIncompleteFiles):
	_in(in),
	_outDir(outputDir),
	_flattenDirs(flattenDirs),
	_keepIncompleteFiles(keepIncompleteFiles),
	_mapping()
{
	_outDir.makeAbsolute();
	poco_assert (_in.good());
	Poco::File tmp(_outDir);
	if (!tmp.exists())
	{
		tmp.createDirectories();
	}
	if (!tmp.isDirectory())
		throw Poco::IOException("Failed to create/open directory: " + _outDir.toString());
	EOk += Poco::Delegate<Decompress, std::pair<const ZipLocalFileHeader, const Poco::Path> >(this, &Decompress::onOk);

}


Decompress::~Decompress()
{
	EOk -= Poco::Delegate<Decompress, std::pair<const ZipLocalFileHeader, const Poco::Path> >(this, &Decompress::onOk);
}


ZipArchive Decompress::decompressAllFiles()
{
	poco_assert (_mapping.empty());
	ZipArchive arch(_in, *this);
	return arch;
}


bool Decompress::handleZipEntry(std::istream& zipStream, const ZipLocalFileHeader& hdr)
{
	if (hdr.isDirectory())
	{
		// directory have 0 size, nth to read
		if (!_flattenDirs)
		{
			std::string dirName = hdr.getFileName();
			if (dirName.find(ZipCommon::ILLEGAL_PATH) != std::string::npos)
				throw ZipException("Illegal entry name " + dirName + " containing " + ZipCommon::ILLEGAL_PATH);
			Poco::Path dir(dirName);
			dir.makeDirectory();
			Poco::File aFile(dir);
			aFile.createDirectories();
		}
		return true;
	}
	try
	{
		std::string fileName = hdr.getFileName();
		if (_flattenDirs)
		{
			// remove path info
			Poco::Path p(fileName);
			p.makeFile();
			fileName = p.getFileName();
		}

		if (fileName.find(ZipCommon::ILLEGAL_PATH) != std::string::npos)
			throw ZipException("Illegal entry name " + fileName + " containing " + ZipCommon::ILLEGAL_PATH);

		Poco::Path file(fileName);
		file.makeFile();
		Poco::Path dest(_outDir, file);
		dest.makeFile();
		if (dest.depth() > 0)
		{
			Poco::File aFile(dest.parent());
			aFile.createDirectories();
		}
		std::ofstream out(dest.toString().c_str(), std::ios::binary);
		if (!out)
		{
			std::pair<const ZipLocalFileHeader, const std::string> tmp = std::make_pair(hdr, "Failed to open output stream " + dest.toString());
			EError.notify(this, tmp);
			return false;
		}
		ZipInputStream inp(zipStream, hdr, false);
		Poco::StreamCopier::copyStream(inp, out);
		out.close();
		Poco::File aFile(file);
		if (!aFile.exists() || !aFile.isFile())
		{
			std::pair<const ZipLocalFileHeader, const std::string> tmp = std::make_pair(hdr, "Failed to create output stream " + dest.toString());
			EError.notify(this, tmp);
			return false;
		}

		if (!inp.crcValid())
		{
			if (!_keepIncompleteFiles)
				aFile.remove();
			std::pair<const ZipLocalFileHeader, const std::string> tmp = std::make_pair(hdr, "CRC mismatch. Corrupt file: " + dest.toString());
			EError.notify(this, tmp);
			return false;
		}

		// cannot check against hdr.getUnCompressedSize if CRC and size are not set in hdr but in a ZipDataInfo
		// crc is typically enough to detect errors
		if (aFile.getSize() != hdr.getUncompressedSize() && !hdr.searchCRCAndSizesAfterData())
		{
			if (!_keepIncompleteFiles)
				aFile.remove();
			std::pair<const ZipLocalFileHeader, const std::string> tmp = std::make_pair(hdr, "Filesizes do not match. Corrupt file: " + dest.toString());
			EError.notify(this, tmp);
			return false;
		}

		std::pair<const ZipLocalFileHeader, const Poco::Path> tmp = std::make_pair(hdr, file);
		EOk.notify(this, tmp);
	}
	catch (Poco::Exception& e)
	{
		std::pair<const ZipLocalFileHeader, const std::string> tmp = std::make_pair(hdr, "Exception: " + e.displayText());
		EError.notify(this, tmp);
		return false;
	}
	catch (...)
	{
		std::pair<const ZipLocalFileHeader, const std::string> tmp = std::make_pair(hdr, "Unknown Exception");
		EError.notify(this, tmp);
		return false;
	}

	return true;
}


void Decompress::onOk(const void*, std::pair<const ZipLocalFileHeader, const Poco::Path>& val)
{
	_mapping.insert(std::make_pair(val.first.getFileName(), val.second));
}


} } // namespace Poco::Zip
