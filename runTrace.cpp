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
		curASU = atoi(curRow[ASU].c_str());
		curLBA = atoi(curRow[LBA].c_str());
		curSIZE = atoi(curRow[SIZE].c_str());
		if(curASU > ret.largestASU) ret.largestASU = curASU;
		if(curLBA > ret.largestLBA) ret.largestLBA = curLBA;
		if(curSIZE > ret.largestSIZE) ret.largestSIZE = curSIZE;
		if(curLBA + curSIZE > ret.largestOffset) ret.largestOffset = curLBA + curSIZE;
		{
			curTIME = atof(curRow[TIME].c_str());
			if(curTIME > ret.largestTIME) ret.largestTIME = curTIME;
			ret.deltaT += curTIME - lastTIME;
			lastTIME = curTIME;
		}
		if((curRow[OPCODE][0] == 'R') || (curRow[OPCODE][0] == 'r')) ret.numReads++;
		ret.numTX++;

	}
	if(ret.numTX) ret.deltaT /= ret.numTX;
	return ret;
}

double runTX(FILE *fh,uint64_t offset, uint64_t size, bool isRead, char *buf) {
	auto startTime = std::chrono::steady_clock::now();
	if(fseek(fh,offset,SEEK_SET)) return -1;
	if(isRead) { if(fread(buf,1,size,fh) != size) return -1; }
	else       { if(fwrite(buf,1,size,fh) != size) return -1; }
	std::chrono::duration<double,std::micro> ret = std::chrono::steady_clock::now() - startTime;
	return ret.count();
}

using namespace std;
int main(int argc, char *argv[]) {
	if(argc < 3) { printf("Usage: %s <traceFile> <device> [timeOut]\n",argv[0]); return 1; }

	FILE *fh = fopen(argv[2],"w+");
	if(!fh) { cerr << "Cannot open: " << argv[2] << endl; return -1; }

	stats_t stats = getStats(argv[1]);

	cout << "Largest ASU=" << stats.largestASU << " offset=" << stats.largestLBA << " size=" << stats.largestSIZE << " time=" << stats.largestTIME << endl;
	cout << "numREAD=" << stats.numReads << " numTX=" << stats.numTX << " deltaT=" << stats.deltaT << endl;

	cout << "Minimum disk size: " << stats.largestASU * stats.largestOffset / (1024*1024*1024) << "GB" << endl;
	cout << "Estimated runtime: " << floor(stats.largestTIME / (60*60)) << "h " << (uint64_t)floor(stats.largestTIME / 60)%60 << 'm' << endl;
	cout << "Avg TX per second: " << stats.numTX / stats.largestTIME << endl;
	cout << "Read/Write ratio : " << (double)stats.numReads / (stats.numTX - stats.numReads) << endl;
	cout << "Percent Read     : " << (double)stats.numReads / stats.numTX << endl;
	cout << "Percent Writes   : " << (1 - (double)stats.numReads / stats.numTX) << endl;

	cout.flush();

	std::unique_ptr<char[]> bigBuf = std::make_unique<char[]>(stats.largestSIZE);
	{
		char *bigBufPtr = bigBuf.get();
		for(uint64_t i = stats.largestSIZE; i; i--) *(bigBufPtr++) = (char)rand();
	}

	uint64_t timeOut = -1;
	if(argc > 3) { timeOut = atoi(argv[3]); cout << "setting timeout=" << timeOut << " seconds" << endl; }

	std::ifstream file(argv[1]);
	CSVRow curRow;
	uint64_t curASU;
	uint64_t curLBA;
	uint64_t curSIZE;
	double curTIME;
	double curDuration;
	double totDuration = 0;
	auto startTime = std::chrono::steady_clock::now();
	while(file >> curRow) {
		curASU = atoi(curRow[ASU].c_str());
		curLBA = atoi(curRow[LBA].c_str());
		curSIZE = atoi(curRow[SIZE].c_str());
		curTIME = atof(curRow[TIME].c_str());
		if((timeOut > 0) && (curTIME > timeOut)) { cout << "Timeout reached." << endl; break; }
		std::this_thread::sleep_until(startTime + std::chrono::microseconds((uint64_t)(curTIME*1000*1000)));
		curDuration = runTX(fh,curASU*stats.largestOffset+curLBA,curSIZE, ((curRow[OPCODE][0]=='R')||(curRow[OPCODE][0]=='r')) ,bigBuf.get());
		if(curDuration < 0) { cerr << "Error with TX(" << curASU << ',' << curLBA << ',' << curSIZE << ',' << curTIME << "): " << strerror(errno) << endl; break; }
		totDuration += curDuration;
	}
	cout << "total duration: " << totDuration << endl;
	return 0;
}
