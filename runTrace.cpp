/* Created by: Andras Fekete
	 Copyright 2016

	 This program is free software: you can redistribute it and/or modify
	 it under the terms of the GNU General Public License as published by
	 the Free Software Foundation, either version 3 of the License, or
	 (at your option) any later version.

	 This program is distributed in the hope that it will be useful,
	 but WITHOUT ANY WARRANTY; without even the implied warranty of
	 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	 GNU General Public License for more details.

	 You should have received a copy of the GNU General Public License
	 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <iterator>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <stdint.h>
#include <sys/types.h>
#include <math.h>
#include <chrono>
#include <memory>
#include <string.h>
#include <thread>
#include <iomanip>

// class stolen from: http://stackoverflow.com/questions/1120140/how-can-i-read-and-parse-csv-files-in-c
class CSVRow {
	public:
		std::string const& operator[](std::size_t index) const { return m_data[index]; }
		std::size_t size() const { return m_data.size(); }
		void readNextRow(std::istream& str) {
			std::string         line;
			std::getline(str,line);

			std::stringstream   lineStream(line);
			std::string         cell;

			m_data.clear();
			while(std::getline(lineStream,cell,',')) m_data.push_back(cell);
		}
	private:
		std::vector<std::string>    m_data;
		friend std::istream& operator>>(std::istream& str,CSVRow& data) { data.readNextRow(str); return str; }   
};

enum { ASU, LBA, SIZE, OPCODE, TIME, OTHER };

typedef struct {
	uint largestASU = 0;
	uint64_t largestLBA = 0;
	uint64_t largestSIZE = 0;
	uint64_t largestOffset = 0;
	double largestTIME = 0;
	uint64_t numReads = 0;
	uint64_t numTX = 0;
	double deltaT = 0;
} stats_t;

stats_t getStats(char *fn) {
	std::ifstream file(fn);
	CSVRow curRow;
	stats_t ret;
	uint64_t curASU;
	uint64_t curLBA;
	uint64_t curSIZE;
	double curTIME;
	double lastTIME = 0;
	while(file >> curRow) {
		curASU = atol(curRow[ASU].c_str());
		curLBA = atol(curRow[LBA].c_str());
		curSIZE = atol(curRow[SIZE].c_str());
		if(curASU > ret.largestASU) ret.largestASU = curASU;
		if(curLBA > ret.largestLBA) ret.largestLBA = curLBA;
		if(curSIZE > ret.largestSIZE) ret.largestSIZE = curSIZE;
		if(curLBA + curSIZE > ret.largestOffset) ret.largestOffset = curLBA + curSIZE;
		{
			curTIME = atof(curRow[TIME].c_str());
			if(curTIME > ret.largestTIME) ret.largestTIME = curTIME;
			ret.deltaT = (ret.deltaT + curTIME - lastTIME) / 2;
			lastTIME = curTIME;
		}
		if((curRow[OPCODE][0] == 'R') || (curRow[OPCODE][0] == 'r')) ret.numReads++;
		ret.numTX++;

	}
	return ret;
}

uint64_t bytesRead = 0;
uint64_t bytesWritten = 0;
int64_t runTX(FILE *fh,uint64_t offset, uint64_t size, bool isRead, char *buf, std::chrono::steady_clock::time_point beginTime) {
	auto startTime = std::chrono::steady_clock::now();
	if(beginTime > startTime) {
		std::this_thread::sleep_until(beginTime);
		startTime = std::chrono::steady_clock::now();
	}
	if(fseek(fh,offset,SEEK_SET)) return -1;
	if(isRead) { if(fread(buf,1,size,fh) != size) return -1; bytesRead+=size; }
	else       { if(fwrite(buf,1,size,fh) != size) return -1; bytesWritten+=size; }
	return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startTime).count();
}

using namespace std;
int main(int argc, char *argv[]) {
	if(argc < 3) {
		cout << "Usage: " << argv[0] << " <traceFile> <device> [[timeIn] timeOut]" << endl;
		cout << "If timeOut < 0, then the transactions will be executed as quickly as possible." << endl;
		return 1;
	}

	FILE *fh = fopen(argv[2],"w+");
	if(!fh) { cerr << "Cannot open: " << argv[2] << endl; return -1; }

	auto startTime = std::chrono::steady_clock::now();
	stats_t stats = getStats(argv[1]);
	cout << "Initialization complete. Took " << std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startTime).count() << " seconds." << endl;

	cout << "Largest ASU=" << stats.largestASU << " offset=" << stats.largestLBA << " size=" << stats.largestSIZE << " time=" << stats.largestTIME << endl;
	cout << "numREAD=" << stats.numReads << " numTX=" << stats.numTX << " deltaT=" << stats.deltaT << endl;

	cout << fixed << setw(10) << setprecision(5);

	cout << "Minimum disk size : " << (double)stats.largestASU * stats.largestOffset / (1024*1024*1024) << "GB" << endl;
	cout << "Data file runtime : " << (uint64_t)floor(stats.largestTIME / (60*60)) << "h " << (uint64_t)floor(stats.largestTIME / 60)%60 << 'm' << endl;
	cout << "Avg TX per second : " << stats.numTX / stats.largestTIME << endl;
	cout << "Avg arrival rate  : " << stats.largestTIME / stats.numTX << endl;
	cout << "True arrival rate : " << stats.deltaT << endl;
	cout << "Read/Write ratio  : " << (double)stats.numReads / (stats.numTX - stats.numReads) << endl;
	cout << "Percent Read      : " << 100*(double)stats.numReads / stats.numTX << endl;
	cout << "Percent Writes    : " << 100*(1 - (double)stats.numReads / stats.numTX) << endl;

	cout.flush();

	std::unique_ptr<char[]> bigBuf = std::make_unique<char[]>(stats.largestSIZE);
	{
		char *bigBufPtr = bigBuf.get();
		for(uint64_t i = stats.largestSIZE; i; i--) *(bigBufPtr++) = (char)rand();
	}

	int64_t timeIn = 0;
	int64_t timeOut = UINT64_MAX;
	bool runFast = false;
	if(argc > 3) {
		timeOut = atol(argv[3])*1000*1000;
		if(argc > 4) {
			timeIn = timeOut;
			timeOut = atol(argv[4])*1000*1000;
		}
		if(timeOut < 0) { runFast = true; timeOut *= -1; cout << "Fast run" << endl; }
		cout << "Setting time range: " << (timeIn / (1000*1000)) << '-' << (timeOut / (1000*1000)) << endl;
	}

	std::ifstream file(argv[1]);
//	string ofn(argv[1]);
//	ofn.append(".log");
//	std::ofstream outFile(ofn);
	CSVRow curRow;
	uint64_t curASU;
	uint64_t curLBA;
	uint64_t curSIZE;
	uint64_t curTIME;
	int64_t curDuration;
	int64_t totDuration = 0;
	double totSpeed = 0;
	uint64_t numTX = 0;
	startTime = std::chrono::steady_clock::now();
	int count = 0;
	while(file >> curRow) {
		curASU = atol(curRow[ASU].c_str());
		curLBA = atol(curRow[LBA].c_str());
		curSIZE = atol(curRow[SIZE].c_str());
		curTIME = atof(curRow[TIME].c_str())*1000*1000;
		if(curTIME < (uint64_t)timeIn) continue;
		if(curTIME > (uint64_t)timeOut) { cout << "Timeout reached." << endl; break; }
		if(runFast) curDuration = runTX(fh,curASU*stats.largestOffset+curLBA,curSIZE, ((curRow[OPCODE][0]=='R')||(curRow[OPCODE][0]=='r')) ,bigBuf.get(), startTime);
		else curDuration = runTX(fh,curASU*stats.largestOffset+curLBA,curSIZE, ((curRow[OPCODE][0]=='R')||(curRow[OPCODE][0]=='r')) ,bigBuf.get(), startTime + std::chrono::microseconds(curTIME));
		if(curDuration < 0) { cerr << "Error with TX(" << curASU << ',' << curLBA << ',' << curSIZE << ',' << curTIME << ") = " << curDuration << ": " << strerror(errno) << endl; break; }
		totDuration += curDuration;
		if(curDuration) totSpeed += (double)curSIZE / curDuration;
		else totSpeed += (double)curSIZE / 1; // if duration is less than 1uS
		if(count == 10000) { cout << "Complete: " << 100*(double)numTX / stats.numTX << "%\r" << flush; count = 0; }
//		outFile << curASU << ',' << curLBA << ',' << curSIZE << ',' << curTIME << ',' << curDuration << endl;
		numTX++;
	}
	cout << "program runtime  : " << ((double)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count() / 1000) << "s" << endl;
	if(numTX) {
		cout << "total duration   : " << totDuration << "us" << endl;
		cout << "avg duration     : " << totDuration / numTX << "us" << endl;
		cout << "avg speed        : " << totSpeed / (1024*1024*numTX) << "MB/s" << endl;
		cout << "numTX=" << numTX << " read=" << (double)bytesRead/(1024*1024) << "MB wrote=" << (double)bytesWritten/(1024*1024) << "MB" << endl;
	} else { cout << "No transactions executed." << endl; }
	file.close();
//	outFile.close();
	return 0;
}
