#include "BigQ.h"
#include "DBFile.h"
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <queue>

OrderMaker *sortorder = NULL;

// this function compares 2 records and returns saying which one is 
int compareRecs(const void *rec1, const void *rec2) {

	// if rec1 is smaller return -1, if equal return 0, else 1
	ComparisonEngine cEng;
	Record *r1 = (Record *)rec1;
	Record *r2 = (Record *)rec2;

	return cEng.Compare(r1, r2, sortorder);
}

// this function is for record comparision for the priority queue
class CompareRecsPQ {
	public:
	// the idea is to tell if element 1 has LOWER priority than element2
	bool operator()(pQ &element1, pQ &element2) {
		// the smaller one gets higher priority
		if ((-1) == compareRecs(element1.rec, element2.rec)) {
			return false;
		}

		return true;
	}
};

// this reallocates the memory used (increases to be precise)
Record* increaseSize(Record *ptr, int oldSize, int newSize) {
	
	if (NULL == ptr) {
		ptr = new (std::nothrow) Record[newSize];
		return ptr;
	}

	Record *newPtr = new (std::nothrow) Record[newSize];
	if (NULL == newPtr) {
		return newPtr;
	}

	//cout << "Increasing size from " << oldSize << " to " << newSize << endl;

	//copy everything
	for (int i = 0; i < oldSize; i++) {
		newPtr[i].Copy(&(ptr[i]));
	}

	delete[] ptr;
	return newPtr;	
}

// gives out the first unused page number for a file
int getFreePageNumber(File &fileObj) {
	int pageNum;

	pageNum = fileObj.GetLength() - 1;
	if (pageNum < 0) {
		return 0;
	}

	return pageNum;
}

// gives out the last used page number for a file
int getLastPageNumber(File &fileObj) {
	int pageNum;

	pageNum = fileObj.GetLength() - 2;
	if (pageNum < 0) {
		return 0;
	}

	return pageNum;
}


// this is the thread which will sort all the records and write them
// into the out pipe
void *sorter (void *args) {
	sortArgs *useTheseArgs = (sortArgs *)args;
	Pipe *in = useTheseArgs->in, *out = useTheseArgs->out;
	
	// global variable to pass it to the comparision function
	sortorder = useTheseArgs->sortOrder;

	int runlen = useTheseArgs->runlen;
	int pagesUsedInARun = 0;
	int pageSize = 0;
	int totalPages = 0;
	int totalRecs = 0;
	int totalRuns = 0;
	bool phase1Complete = false;
	int RECORD_SIZE = DEFAULT_RECORD_SIZE;

	File outFile;
	char outFileName[32] = {0};
	strcpy(outFileName, "jflweiunsbf23djlw1.tmp");
	outFile.Open(0, outFileName);

	// this is to temporarily save data on memory
	Record *recBuffer = new (std::nothrow)Record[RECORD_SIZE];
	if (NULL == recBuffer) {
		cout << __FILE__ << "(" <<__LINE__ << ")"
			<< "Failed to allocate memory!" << endl;
		return NULL;
	}

	Record *tmp;
	Record singleRec;
	Page singlePage;

	// validate runlen, if invalid, set it to 5
	if (runlen <= 0) {
		cout << "Warning!: Invalid run length: [" << runlen <<"] provided. "
			<< "Using default run length: [" << DEFAULT_RUN_LEN <<"]" << endl;
		runlen = DEFAULT_RUN_LEN;
	}
	
	//////////////////////////////// phase 1 ////////////////////////////////////
	// accept all pages until the runlength
	while (!phase1Complete) {

		// read 1 record from the input pipe, if you cannot read, that means
		// there are no more records to be provided, phase 1 of sorting is complete
		if (0 == in->Remove(&recBuffer[totalRecs])) {
			phase1Complete = true;
			break;
		}

		// check the length of the record and see if it fits
		// in a page. If it does, include its length into the pageSize
		// as soon as the pageSize reaches its limit, use a new page and mark pageSize = 0.
		// if the pagesUsed reach the runlen limit provided by the user, sort each record
		// dump them into a temporary file and start a new run.
		if (pageSize + (((int *)recBuffer[totalRecs].bits)[0]) <= PAGE_SIZE) {
			pageSize += (((int *)recBuffer[totalRecs].bits)[0]);
		} else {
			pagesUsedInARun++;
			pageSize = 0;
			// check if you have already have runlen worth of records
			if (pagesUsedInARun >= runlen) {
				// this means that now we should start a new run
				/*
				cout << "Sorting: totalRecs: " << totalRecs << endl;
				cout << "Sorting: pagesUsedInARun " << pagesUsedInARun << endl;
				cout << "Sorting: runlen: " << runlen << endl;*/

				// sort all records
				qsort(recBuffer, totalRecs, sizeof(Record), compareRecs);

				// dump everything into a file
				for (int i = 0; i < totalRecs; i++) {
					// if you are not able to append (probably the page is full)
					if (0 == singlePage.Append(&(recBuffer[i]))) {
						// this gives the page number which is free
						int pageNum = getFreePageNumber(outFile);
						//cout << "******* Added Pagenum: " << pageNum << endl;
						//cout << "******* Added Recs: " << i << endl;
						outFile.AddPage(&singlePage, pageNum);
						singlePage.EmptyItOut();
						if (0 == singlePage.Append(&(recBuffer[i]))) {
							cout << "Error " << __LINE__ << endl;
						}
					}
				}

				// the record which was removed from the pipe and not added
				// to a page, now becomes the first record of the next run
				recBuffer[0].Copy(&(recBuffer[totalRecs]));
				pageSize += (((int *)recBuffer[0].bits)[0]);

				pagesUsedInARun = 0;
				totalRecs = 0;
				totalRuns++;
			}
		}
	
		// store everything in an array, if you run out of memory
		// reallocate more memory (add the record anyway, even though
		// it would not fit in the page, it will fit in the next page)!
		totalRecs++;
		if (totalRecs == RECORD_SIZE) { // you have run out of memory

			/*
			for (int i = 0; i < totalRecs; i++) {
				recBuffer[i].Print(s);
			}*/

			tmp = increaseSize(recBuffer, RECORD_SIZE, (2 * RECORD_SIZE));
			if (NULL == tmp) {
				cout << __FILE__ << "(" <<__LINE__ << ")"
					<< "Failed to allocate memory!" << endl;

				delete[] recBuffer;
				outFile.Close();
				out->ShutDown();
				return NULL;
			}

			recBuffer = tmp;
			tmp = NULL;
			RECORD_SIZE *= 2;

			/*
			for (int i = 0; i < totalRecs; i++) {
				recBuffer[i].Print(s);
			}*/
		}
	}

	// this is the condition where you have records less than a run length
	// they were never written onto any page. write them now.
	if (totalRecs) {
		// sort all records
		qsort(recBuffer, totalRecs, sizeof(Record), compareRecs);

		// dump everything into a file
		for (int i = 0; i < totalRecs; i++) {
			if (0 == singlePage.Append(&(recBuffer[i]))) {
				// this gives the page number which is free
				int pageNum = getFreePageNumber(outFile);
				//cout << "####### Added Pagenum: " << pageNum << endl;
				//cout << "####### Added Recs: " << i << endl;
				outFile.AddPage(&singlePage, pageNum);
				singlePage.EmptyItOut();
				if (0 == singlePage.Append(&(recBuffer[i]))) {
					cout << "Error " << __LINE__ << endl;
					exit(1);
				}
			}

		}

		// insert the record if present into the file
		int pageNum = getFreePageNumber(outFile);
		outFile.AddPage(&singlePage, pageNum);
		singlePage.EmptyItOut();
		
		totalRuns++;
	}

	// now all the runs are sorted, phase 1 is complete, phase 2 can be started
	// release memory which was used in phase 1
	delete[] recBuffer;

	//////////////////////////// phase 2 //////////////////////////////////////
	// merge all runs and write them out into the out pipe
	// take a single record from the multiple of runlength pages,
	// merge them and put it into the out pipe

	priority_queue<pQ, vector<pQ>, CompareRecsPQ> pq;

	// each run has 1 record buffer. record from the pages
	// should be read into its own buffer
	recBuffer = new (std::nothrow) Record[totalRuns];
	if (NULL == recBuffer) {
		cout << "Error" << endl;
		outFile.Close();
		out->ShutDown();
		return NULL;
	}

	// each run has 1 page buffer. the records for that run
	// should be read from its own pageBuffer
	Page *pageBuffer = new (std::nothrow) Page[totalRuns];
	if (NULL == pageBuffer) {
		cout << "Error" << endl;
		delete[] recBuffer;
		outFile.Close();
		out->ShutDown();
		return NULL;
	}

	// each run has its corresponding pageNumber which tells
	// the run from which page should it read the record
	int *runPageNum = new (std::nothrow) int[totalRuns];
	if (NULL == runPageNum) {
		cout << "Error" << endl;
		delete[] recBuffer;
		delete[] pageBuffer;
		outFile.Close();
		out->ShutDown();
		return NULL;
	}

	// load atleast 1 page of each record
	for (int i = 0; i < totalRuns; i++) {
		//cout << "Reading page " << (i*runlen) << " for run " << i << endl;
		outFile.GetPage(&pageBuffer[i], (i*runlen));
		// stores the pagenumber of each run, if -1 there are no more records in that run
		runPageNum[i] = (i*runlen);
	}

	//insert 1 element from each run
	pQ recBlock;
	for (int i = 0; i < totalRuns; i++) {
		//cout << "Reading record from run " << i << endl;
		if (0 == pageBuffer[i].GetFirst(&recBuffer[i])) {
			cout << "Error " << __FILE__ << " (" << __LINE__ << ")" << endl;
		}
		recBlock.rec = &recBuffer[i];
		recBlock.runIndex = i;
		pq.push(recBlock);
	}

	// remove records from the priority queue and insert them into the out pipe.
	// when removing the record, see the run number to which the record belongs to
	// and then insert another record from that run into the priority queue.

	// recBlock structure has a pointer to the record (*rec) and the run number
	// to which it belongs to (runIndex). runPageNum tells you for each run, which
	// page number to read from. If it is (-1), it means that there are no more records
	while (!pq.empty()) {
		recBlock = pq.top();
		out->Insert(recBlock.rec);
		pq.pop();

		//fill in a record into the priority queue
		int runIdx = recBlock.runIndex;
		//cout << "Got record from run " << runIdx << endl; 
		if (runPageNum[runIdx] >= 0) { // this means that the run has more records
			// get the next record from the page
			if (pageBuffer[runIdx].GetFirst(&recBuffer[runIdx])) {
				recBlock.rec = &recBuffer[runIdx];
				recBlock.runIndex = runIdx;
				pq.push(recBlock);
				continue;
			}
		
			// if the run is over, mark the index
			//cout << "Page " << runPageNum[runIdx] << " got over!" <<  endl;
			runPageNum[runIdx]++;
			if (0 == (runPageNum[runIdx] % runlen)) {
				runPageNum[runIdx] = (-1);
				continue;
			}

			// if the page ran out of records and the run
			// is not over, get the next page
			if (runPageNum[runIdx] > (getLastPageNumber(outFile))) {
				// no more pages
				continue;
			}
			outFile.GetPage(&pageBuffer[runIdx], runPageNum[runIdx]);
			if (pageBuffer[runIdx].GetFirst(&recBuffer[runIdx])) {
				recBlock.rec = &recBuffer[runIdx];
				recBlock.runIndex = runIdx;
				pq.push(recBlock);
			} else {
				cout << "Error " << __FILE__ << " (" << __LINE__ << ")" << endl;
				break;
			}
		}
	}

	// release all the resources once done
	outFile.Close();
	remove(outFileName);
	out->ShutDown();

	delete[] recBuffer;
	delete[] pageBuffer;
	delete[] runPageNum;
}

BigQ :: BigQ (Pipe &in, Pipe &out, OrderMaker &sortorder, int runlen) {
	
	//sortArgs args = {&in, &out, &sortorder, runlen};
	args = new sortArgs;
	args->in = &in;
	args->out = &out;
	args->sortOrder = &sortorder;
	args->runlen = runlen;

	//fork a process that will perform the operation and return from here
	pthread_create(&sorterThread, NULL, sorter, (void *)args);
}

BigQ::~BigQ () {
	pthread_join(sorterThread, NULL);
	delete args;
}