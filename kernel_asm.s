
	.def setTMPLbit
	.def setASPbit
	.def setPSP
	.def getPSP
	.def getMSP

.thumb
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

