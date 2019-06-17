/******************************************************************************
*
* Copyright (C) 2009 - 2014 Xilinx, Inc.  All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* Use of the Software is limited solely to applications:
* (a) running on a Xilinx device, or
* (b) that interact with a Xilinx device through a bus or interconnect.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* XILINX  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
* OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Except as contained in this notice, the name of the Xilinx shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in
* this Software without prior written authorization from Xilinx.
*
******************************************************************************/

/*
 * helloworld.c: simple test application
 *
 * This application configures UART 16550 to baud rate 9600.
 * PS7 UART (Zynq) is not initialized by this application, since
 * bootrom/bsp configures it to baud rate 115200
 *
 * ------------------------------------------------
 * | UART TYPE   BAUD RATE                        |
 * ------------------------------------------------
 *   uartns550   9600
 *   uartlite    Configurable only in HW design
 *   ps7_uart    115200 (configured by bootrom/bsp)
 */

#include <stdio.h>
#include "platform.h"
#include "xparameters.h"
#include "xiic.h"
#include "xaxidma.h"
#include "xintc.h"
#include "microblaze_sleep.h"
#include "xil_printf.h"

#define BINARY_PATTERN "%c%c%c%c%c%c%s%c%c%c%c%c%c%c%c%c%c%s%c%c%c%c%c%c%s%c%c%c%c%c%c%c%c%c%c"
#define BINARIZE(val) \
	((val >> 31) % 2 ? '1' : '0'), \
	((val >> 30) % 2 ? '1' : '0'), \
	((val >> 29) % 2 ? '1' : '0'), \
	((val >> 28) % 2 ? '1' : '0'), \
	((val >> 27) % 2 ? '1' : '0'), \
	((val >> 26) % 2 ? '1' : '0'), \
	" - ", \
	((val >> 25) % 2 ? '1' : '0'), \
	((val >> 24) % 2 ? '1' : '0'), \
	((val >> 23) % 2 ? '1' : '0'), \
	((val >> 22) % 2 ? '1' : '0'), \
	((val >> 21) % 2 ? '1' : '0'), \
	((val >> 20) % 2 ? '1' : '0'), \
	((val >> 19) % 2 ? '1' : '0'), \
	((val >> 18) % 2 ? '1' : '0'), \
	((val >> 17) % 2 ? '1' : '0'), \
	((val >> 16) % 2 ? '1' : '0'), \
	" - ", \
	((val >> 15) % 2 ? '1' : '0'), \
	((val >> 14) % 2 ? '1' : '0'), \
	((val >> 13) % 2 ? '1' : '0'), \
	((val >> 12) % 2 ? '1' : '0'), \
	((val >> 11) % 2 ? '1' : '0'), \
	((val >> 10) % 2 ? '1' : '0'), \
	" - ", \
	((val >> 9) % 2 ? '1' : '0'), \
	((val >> 8) % 2 ? '1' : '0'), \
	((val >> 7) % 2 ? '1' : '0'), \
	((val >> 6) % 2 ? '1' : '0'), \
	((val >> 5) % 2 ? '1' : '0'), \
	((val >> 4) % 2 ? '1' : '0'), \
	((val >> 3) % 2 ? '1' : '0'), \
	((val >> 2) % 2 ? '1' : '0'), \
	((val >> 1) % 2 ? '1' : '0'), \
	((val >> 0) % 2 ? '1' : '0')

// 512 beats per packet (2KB)

#define HEAP_SIZE		0x4000		// 16KB = 8 packets
#define XFER_READS		0x200		// 512 beats/xfer = 1 packet/xfer (<-- the key to success is transferring exactly one packet per transfer)
#define NUM_XFERS		2			// 2 xfer
#define DATA_WIDTH		32

#define RINGSPACE_BASE	0xA0000000
#define RINGSPACE		0x1000
#define HEAP_BASE		0xA1000000

#define IIC_DEV_ID		XPAR_AXI_IIC_0_DEVICE_ID
#define DMA_DEV_ID 		XPAR_AXIDMA_0_DEVICE_ID
#define INTC_DEVICE_ID  XPAR_INTC_0_DEVICE_ID
#define VEC_ID			XPAR_AXI_INTC_0_AXI_DMA_0_S2MM_INTROUT_INTR



u32 *heap;
u32 *ringspace;
XAxiDma_Config	*DmaConfig;
XAxiDma 		Dma;
XIntc			Intc;
XIic			Iic;
int transfers_left = NUM_XFERS;

XAxiDma_Bd *FirstBdPtr;
u32 freeBdCount;
XAxiDma_BdRing *ringPtr;


int initIic();
int initDma();
int initInterruptController();
int setupRing();
int launchRing();
void waitDma();

/*void transfer(u32 start, int num_reads) {
	 transfers_left--;
	 XAxiDma_SimpleTransfer(&Dma, start, num_reads*DATA_WIDTH/8, XAXIDMA_DEVICE_TO_DMA);
}

static void Handler(void *InstancePtr) {
	// Read and acknowledge interrupt
	//u32 pending;
	//pending = XAxiDma_IntrGetIrq((XAxiDma*)InstancePtr, XAXIDMA_DEVICE_TO_DMA);
	//XAxiDma_IntrAckIrq((XAxiDma*)InstancePtr, pending, XAXIDMA_DEVICE_TO_DMA);

	//xil_printf("\n\nINTERRUPT HAPPENING!\n\n");
	heap[0] = -1;
	heap[9] = -2;

	// start a new transfer if needed
	if(transfers_left == 0) {
		return;
	}

	transfer((u32) &(heap[(NUM_XFERS-transfers_left)*XFER_READS]), XFER_READS);

	return;
}
*/

int main()
{
	//int RingIndex = 0;

	int status;
	//heap = (u32*)malloc(HEAP_SIZE);
	heap = (u32*)HEAP_BASE;

    init_platform();
    print("\n###################################\n\rWelcome to the DMA Transfer Tester\n\r###################################\n\r");

    // Initialize I2C master hub
    status = initIic();
    if(status != XST_SUCCESS) {
    	xil_printf("Failed to initialize I2C\r\n");
    	return XST_FAILURE;
    }

    // Initialize DMA
    status = initDma();
    if(status != XST_SUCCESS) {
    	xil_printf("Failed to initialize DMA\r\n");
    	return XST_FAILURE;
    }

    /*
    // Initialize Interrupt Controller
    status = initInterruptController();
    if(status != XST_SUCCESS) {
    	xil_printf("Failed to initialize the interrupt controller\r\n");
    	return XST_FAILURE;
    }
*/

    // Setup the ring
    status = setupRing();
    if(status != XST_SUCCESS) {
    	xil_printf("Failed to setup the ring\r\n");
    	return XST_FAILURE;
    }

	// Clear heap area for debugging
	memset((void *)heap, 0, HEAP_SIZE);

	// Clear the data cache and reset DMA core
	Xil_DCacheFlushRange((UINTPTR)heap, XFER_READS*NUM_XFERS*DATA_WIDTH/8);
	XAxiDma_Reset(&Dma);
	while(!XAxiDma_ResetIsDone(&Dma));

	/*
	// Start first transfer... remaining transfers will be triggered automatically by TLAST signal interrupts
	transfer((u32) &(heap[(NUM_XFERS-transfers_left)*XFER_READS]), XFER_READS);
	//status = XAxiDma_SimpleTransfer(&Dma, (u32) heap, XFER_READS*DATA_WIDTH/8, XAXIDMA_DEVICE_TO_DMA);
	//if (status != XST_SUCCESS) {
	//	xil_printf("XFER failed %s\r\n", status == XST_FAILURE ? "XST_FAILURE" : "XST_INVALID_PARAM");
	//	return XST_FAILURE;
	//}
	usleep_MB(100000);
	*/

	// Present the ring to the hardware to begin transfers
	status = launchRing();
    if(status != XST_SUCCESS) {
    	xil_printf("Failed to setup the ring\r\n");
    	return XST_FAILURE;
    }

    waitDma();

	// Print out results
	Xil_DCacheFlushRange((UINTPTR)heap, XFER_READS*NUM_XFERS*DATA_WIDTH/8);
	for(int i=0; i<XFER_READS*NUM_XFERS; i++) {
		xil_printf("loc %d: "BINARY_PATTERN"\n\r", i, BINARIZE(heap[i]));
	}


    cleanup_platform();
    return 0;
}

int initIic() {


	return XST_SUCCESS;
}

int initDma() {
	int status;

	// Look up DMA Config object
	    DmaConfig = XAxiDma_LookupConfig(DMA_DEV_ID);
	    if (!DmaConfig) {
	    	printf("No config found for %d\r\n", DMA_DEV_ID);
	    	return XST_FAILURE;
	    }

	    // Initialize DMA
		status = XAxiDma_CfgInitialize(&Dma, DmaConfig);
		if (status != XST_SUCCESS) {
			printf("Initialization DMA failed %d\r\n", status);
			return XST_FAILURE;
		}

		// Enable DMA interrupt issuing
		//XAxiDma_IntrEnable(&Dma, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);

		return XST_SUCCESS;
}


/*
int initInterruptController() {
	int status;

    // Initialize the interrupt controller
    status = XIntc_Initialize(&Intc, INTC_DEVICE_ID);
    if (status != XST_SUCCESS) {
    	xil_printf("Failed init intc\r\n");
    	return XST_FAILURE;
    }

    // register the ISR
	status = XIntc_Connect(&Intc, VEC_ID,
			       (XInterruptHandler) Handler, &Dma);
	if (status != XST_SUCCESS) {
		xil_printf("Failed to register interrupt handler\r\n");
		return XST_FAILURE;
	}

	// Start interrupt controller
	status = XIntc_Start(&Intc, XIN_REAL_MODE);
	if (status != XST_SUCCESS) {
		xil_printf("Failed to start intc\r\n");
		return XST_FAILURE;
	}

	// Enable interrupts receiving
	XIntc_Enable(&Intc, VEC_ID);

	// Init exception table and register entry ISR handler
	Xil_ExceptionInit();
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
			(Xil_ExceptionHandler)XIntc_InterruptHandler,
			(void *)&Intc);
	Xil_ExceptionEnable();

	return XST_SUCCESS;
}
*/

int setupRing() {
	int Delay = 0;
	int Coalesce = 1;
	u32 bdCount;
	XAxiDma_Bd BdTemplate;
	XAxiDma_Bd *BdCurPtr;
	int status;

	ringPtr = XAxiDma_GetRxRing(&Dma);
	XAxiDma_BdRingIntDisable(ringPtr, XAXIDMA_IRQ_ALL_MASK);
	XAxiDma_BdRingSetCoalesce(ringPtr, Coalesce, Delay);
	//ringspace = aligned_alloc(XAXIDMA_BD_MINIMUM_ALIGNMENT, RINGSPACE);
	ringspace = (u32*)RINGSPACE_BASE;
	bdCount = XAxiDma_BdRingCntCalc(XAXIDMA_BD_MINIMUM_ALIGNMENT, RINGSPACE);

	if(NUM_XFERS > bdCount) {
		xil_printf("Cannot fit %d Buffer Descriptors in ring with capacity %d\n\r", NUM_XFERS, bdCount);
		return XST_FAILURE;
	}

	status = XAxiDma_BdRingCreate(ringPtr, (u32)ringspace, (u32)ringspace, XAXIDMA_BD_MINIMUM_ALIGNMENT, NUM_XFERS);
	if (status != XST_SUCCESS) {
		xil_printf("RX create BD ring failed %d\r\n", status);
		return XST_FAILURE;
	}

	XAxiDma_BdClear(&BdTemplate);
	status = XAxiDma_BdRingClone(ringPtr, &BdTemplate);
	if (status != XST_SUCCESS) {
		xil_printf("RX clone BD failed %d\r\n", status);
		return XST_FAILURE;
	}

	freeBdCount = XAxiDma_BdRingGetFreeCnt(ringPtr);
	xil_printf("In space of size %d bytes, can fit %d BDs (using %d)\n\r", RINGSPACE, bdCount, freeBdCount);

	// Points BdPtr towards head of ring
	status = XAxiDma_BdRingAlloc(ringPtr, NUM_XFERS, &FirstBdPtr);
	if (status != XST_SUCCESS) {
		xil_printf("RX alloc BD failed %d\r\n", status);
		return XST_FAILURE;
	}

	// Initialize all BDs in ring with write addresses and lengths
	BdCurPtr = FirstBdPtr;
	for (int Index = 0; Index < NUM_XFERS; Index++) {
			status = XAxiDma_BdSetBufAddr(BdCurPtr, (u32)&(heap[Index*XFER_READS]));

			if (status != XST_SUCCESS) {
				xil_printf("Set buffer addr %x on BD %x failed %d\r\n",
				    (unsigned int)&(heap[Index*XFER_READS]),
				    (UINTPTR)BdCurPtr, status);
				return XST_FAILURE;
			}

			status = XAxiDma_BdSetLength(BdCurPtr, XFER_READS*DATA_WIDTH/8, ringPtr->MaxTransferLen);
			if (status != XST_SUCCESS) {
				xil_printf("Rx set length %d on BD %x failed %d\r\n", XFER_READS*DATA_WIDTH/8, (UINTPTR)BdCurPtr, status);
				return XST_FAILURE;
			}

			/* Receive BDs do not need to set anything for the control
			 * The hardware will set the SOF/EOF bits per stream status
			 */
			XAxiDma_BdSetCtrl(BdCurPtr, 0);
			XAxiDma_BdSetId(BdCurPtr, &(heap[Index*XFER_READS]));

			BdCurPtr = (XAxiDma_Bd *)XAxiDma_BdRingNext(ringPtr, BdCurPtr);
		}

	return XST_SUCCESS;
}

int launchRing() {
	int status;
	status = XAxiDma_BdRingToHw(ringPtr, freeBdCount, FirstBdPtr);
	if (status != XST_SUCCESS) {
		xil_printf("RX submit hw failed %d\r\n", status);
		return XST_FAILURE;
	}

	/* Start RX DMA channel */
	status = XAxiDma_BdRingStart(ringPtr);
	if (status != XST_SUCCESS) {
		xil_printf("RX start hw failed %d\r\n", status);
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

void waitDma() {
	int ProcessedBdCount;
	// Busy wait until the desired number of packets have been received
	while ((ProcessedBdCount = XAxiDma_BdRingFromHw(ringPtr, XAXIDMA_ALL_BDS, &FirstBdPtr)) < NUM_XFERS);
}
