
	.def setTMPLbit
	.def setASPbit
	.def setPSP
	.def getPSP
	.def getMSP
	.def pushContext
	.def popContext
	.def fabricateContext
	.def extractSVC

.thumb

.const
xPSRval	.field 0x61000000
LRval	.field 0xFFFFFFFD

.text

setTMPLbit:
	MRS R0, CONTROL
	ORR R1, R0, #1
	MSR CONTROL, R1
	ISB
	BX LR

setASPbit:
	MRS R0, CONTROL
	ORR R1, R0, #2
	MSR CONTROL, R1
	ISB
	BX LR

setPSP:
	MSR PSP, R0
	ISB
	BX LR

getPSP:
	MRS R0, PSP
	BX LR

getMSP:
	MRS R0, MSP
	BX LR

pushContext:
	STMDB R0!, {R4-R11}
	MSR PSP, R0
	BX LR

popContext:
	LDMIA R0!, {R4-R11}
	MSR PSP, R0
	BX LR

fabricateContext:
	MRS R1, PSP
	LDR R2, xPSRval
	STR R2, [R1, #-4]!
	STR R0, [R1, #-4]!
	LDR R2, LRval
	STR R2, [R1, #-4]!
	MOV R2, #0
	STR R2, [R1, #-4]!
	STR R2, [R1, #-4]!
	STR R2, [R1, #-4]!
	STR R2, [R1, #-4]!
	STR R2, [R1, #-4]!
	MSR PSP, R1
	BX LR

extractSVC:
	LDR R0, [R0, #24]
	LDRB R0, [R0, #-2]
	BX LR

