#include "CfuDevice.h"

#include "CfuDevice.tmh"

VOID
CfuDevice_WriteReport(
    _In_ VOID* VhfClientContext,
    _In_ VHFOPERATIONHANDLE VhfOperationHandle,
    _In_ VOID* VhfOperationContext,
    _In_ PHID_XFER_PACKET HidTransferPacket
    )
/*++

Routine Description:

    This function is invoked by Vhf when client wants to send a offer/payload related cmd.
    After receiving the UCSI command, this function will -
        - Populate the command buffer and saves in the BufferQueue
        - Trigger the consumer to process the buffer.
        - Acknowledge Vhf, receipt of command.

Arguments:

    VhfClientContext - This Module's handle.
    VhfOperationHandle - Vhf context for this transaction.
    VhfOperationContext - Client context for this transaction.
    HidTransferPacket - Contains Feature report data.

Return Value:

    None

--*/
{
    WDFDEVICE device;
    NTSTATUS ntStatus;
    PDEVICE_CONTEXT deviceContext;

    VOID* clientBuffer = NULL;
    VOID* clientBufferContext = NULL;

    UNREFERENCED_PARAMETER(VhfOperationContext);

    FuncEntry(TRACE_DEVICE);

    ntStatus = STATUS_INVALID_DEVICE_REQUEST;
    device = (WDFDEVICE) VhfClientContext;
    deviceContext = DeviceContextGet(device);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "CfuDevice_WriteReport");

    if (HidTransferPacket->reportBufferLen < sizeof(OUTPUT_REPORT_LENGTH))
    {
        goto Exit;
    }

    if (HidTransferPacket->reportId != REPORT_ID_PAYLOAD_OUTPUT && HidTransferPacket->reportId != REPORT_ID_OFFER_OUTPUT)
    {
        goto Exit;
    }

    // Get a buffer from BufferPool.
    //
    ntStatus = DMF_BufferQueue_Fetch(deviceContext->DmfModuleResponseBufferQueue,
                                     &clientBuffer,
                                     &clientBufferContext);
    if (! NT_SUCCESS(ntStatus))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "DMF_BufferQueue_Fetch ntStatus=0x%x", ntStatus);
        goto Exit;
    }

    ASSERT(clientBuffer != NULL);
    ASSERT(clientBufferContext != NULL);
    RESPONSE_BUFFER* responseBuffer = (RESPONSE_BUFFER*)clientBuffer;
    ULONG* responseBufferSize = (ULONG*)clientBufferContext;

    FWUPDATE_OFFER_RESPONSE offerResponse = { 0 };
    FWUPDATE_CONTENT_RESPONSE contentRespose = { 0 };

    switch (HidTransferPacket->reportId)
    {

    case REPORT_ID_OFFER_OUTPUT:
        // NOTE: This could be either of the below.
        // FWUPDATE_OFFER_COMMAND
        // FWUPDATE_OFFER_INFO_ONLY_COMMAND
        // FWUPDATE_OFFER_EXTENDED_COMMAND
        //     Response: FWUPDATE_OFFER_RESPONSE
        FWUPDATE_OFFER_COMMAND* offerCommand = (FWUPDATE_OFFER_COMMAND*)HidTransferPacket->reportBuffer;
        if (offerCommand->ComponentInfo.ComponentId == 0xFF)
        {
            FWUPDATE_OFFER_INFO_ONLY_COMMAND* offerInformation = (FWUPDATE_OFFER_INFO_ONLY_COMMAND*)HidTransferPacket->reportBuffer;
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "Received Offer Information. Code=0x%x Token = 0x%x", 
                                                               offerInformation->ComponentInfo.InformationCode,
                                                               offerInformation->ComponentInfo.Token);
        }
        else if (offerCommand->ComponentInfo.ComponentId == 0xFE)
        {
            FWUPDATE_OFFER_EXTENDED_COMMAND* offerCommandExt = (FWUPDATE_OFFER_EXTENDED_COMMAND*)HidTransferPacket->reportBuffer;
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "Received Offer Command. Command=0x%x Token = 0x%x",
                                                               offerCommandExt->ComponentInfo.CommmandCode, 
                                                               offerCommandExt->ComponentInfo.Token);
        }
        else
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "Received Offer: \
                                                               Component { Id = 0x%x, V= 0x%x, I = 0x%x, Segment = 0x%x, Token = 0x%x }\
                                                               Version { M = 0x%x, N = 0x%x  variant = 0x%x }",
                                                               offerCommand->ComponentInfo.ComponentId,
                                                               offerCommand->ComponentInfo.ForceIgnoreVersion,
                                                               offerCommand->ComponentInfo.ForceImmediateReset, 
                                                               offerCommand->ComponentInfo.SegmentNumber, 
                                                               offerCommand->ComponentInfo.Token, 
                                                               offerCommand->Version.MajorVersion, 
                                                               offerCommand->Version.MinorVersion, 
                                                               offerCommand->Version.Variant);

            //deviceContext->ComponentVersion.AsUInt32 = offerCommand->Version.AsUInt32;
        }

        // In either of the case send a success response!
        //
        offerResponse.Status = COMPONENT_FIRMWARE_UPDATE_OFFER_ACCEPT;
        offerResponse.Token = offerCommand->ComponentInfo.Token;
        responseBuffer->ResponseType = OFFER;
        responseBuffer->Response = offerResponse.AsUInt16;

        break;
    case REPORT_ID_PAYLOAD_OUTPUT:
        // FWUPDATE_CONTENT_COMMAND
        //     Response: FWUPDATE_CONTENT_RESPONSE

        FWUPDATE_CONTENT_COMMAND* contentCommand = (FWUPDATE_CONTENT_COMMAND*)HidTransferPacket->reportBuffer;

        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "Content Received: \
                                                           { SeqNo = 0x%x Addr = 0x%x, L = 0x%x }",
                                                           contentCommand->SequenceNumber,
                                                           contentCommand->Address, 
                                                           contentCommand->Length);

        if (contentCommand->Flags & COMPONENT_FIRMWARE_UPDATE_FLAG_FIRST_BLOCK)
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "First block  Flag set ");

        if (contentCommand->Flags & COMPONENT_FIRMWARE_UPDATE_FLAG_LAST_BLOCK)
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "Last block Flag set");

        if (contentCommand->Flags & COMPONENT_FIRMWARE_UPDATE_FLAG_VERIFY)
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "Verify Flag set");

        contentRespose.Status = COMPONENT_FIRMWARE_UPDATE_SUCCESS;
        contentRespose.SequenceNumber = contentCommand->SequenceNumber;
        responseBuffer->ResponseType = CONTENT;
        responseBuffer->Response = contentRespose.AsUInt16;

        break;
    }

    // Put the command buffer to the consumer.
    //
    *responseBufferSize = sizeof(RESPONSE_BUFFER);
    DMF_BufferQueue_Enqueue(deviceContext->DmfModuleResponseBufferQueue,
                            clientBuffer);
    ntStatus = STATUS_SUCCESS;

Exit:

    // Acknowledge Vhf, receipt of the write.
    //
    DMF_VirtualHidDeviceVhf_AsynchronousOperationComplete(deviceContext->DmfModuleVirtualHidDeviceVhf,
                                                          VhfOperationHandle,
                                                          ntStatus);

    if (NT_SUCCESS(ntStatus))
    {
        // Trigger the worker thread to start the work
        //
        DMF_Thread_WorkReady(deviceContext->DmfModuleThread);
    }

    FuncExitVoid(TRACE_DEVICE);
}

VOID
CfuDevice_GetFeatureReport(
    _In_ VOID* VhfClientContext,
    _In_ VHFOPERATIONHANDLE VhfOperationHandle,
    _In_ VOID* VhfOperationContext,
    _In_ PHID_XFER_PACKET HidTransferPacket
    )
/*++

Routine Description:

    This function is invoked by Vhf when client wants to send a UCSI command to dmf PlatformPolicyManager.
    It return the feature report, and Acknowledges Vhf, receipt of Get.

Arguments:

    VhfClientContext - This Module's handle.
    VhfOperationHandle - Vhf context for this transaction.
    VhfOperationContext - Client context for this transaction.
    HidTransferPacket - Will Contains Feature report data returned.

Return Value:

    None

--*/
{
    WDFDEVICE device;
    PDEVICE_CONTEXT deviceContext;
    NTSTATUS ntStatus;

    FuncEntry(TRACE_DEVICE);

    UNREFERENCED_PARAMETER(VhfOperationContext);
    UNREFERENCED_PARAMETER(HidTransferPacket);

    ntStatus = STATUS_INVALID_DEVICE_REQUEST;
    device = (WDFDEVICE)VhfClientContext;
    deviceContext = DeviceContextGet(device);

    if (HidTransferPacket->reportBufferLen < sizeof(GET_FWVERSION_RESPONSE))
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "CfuDevice_GetFeatureReport Size Mismatch 0x%x", HidTransferPacket->reportBufferLen);
        goto Exit;
    }

    if (HidTransferPacket->reportId != REPORT_ID_VERSIONS_FEATURE)
    {
        goto Exit;
    }

    ntStatus = STATUS_SUCCESS;

    GET_FWVERSION_RESPONSE firmwareVersionResponse = { 0 };
    firmwareVersionResponse.header.ComponentCount = 1;
    firmwareVersionResponse.header.ProtocolRevision = 02;
    firmwareVersionResponse.componentVersionsAndProperty[0].ComponentVersion.AsUInt32 = deviceContext->ComponentVersion.AsUInt32;
    firmwareVersionResponse.componentVersionsAndProperty[0].ComponentProperty.ComponentId = deviceContext->ComponentId;

    RtlCopyMemory(HidTransferPacket->reportBuffer,
                  &firmwareVersionResponse,
                  sizeof(firmwareVersionResponse));

    HidTransferPacket->reportBufferLen = sizeof(firmwareVersionResponse);
    HidTransferPacket->reportId = REPORT_ID_VERSIONS_FEATURE;

Exit:

    DMF_VirtualHidDeviceVhf_AsynchronousOperationComplete(deviceContext->DmfModuleVirtualHidDeviceVhf,
                                                          VhfOperationHandle,
                                                          ntStatus);

    FuncExitVoid(TRACE_DEVICE);
}

NTSTATUS
CfuDevice_ResponseSend(
    _In_ DMFMODULE DmfModule,
    _In_ RESPONSE_BUFFER* ResponseBuffer
    )
/*++

Routine Description:

    This function completes an input report read.

Arguments:

    DmfModule - This Module's handle.
    ResponseBuffer - Buffer containing the response to be sent.

Return Value:

    NTSTATUS

--*/
{
    NTSTATUS ntStatus;
    WDFDEVICE device;
    PDEVICE_CONTEXT deviceContext;
    UINT8 reportId;

    device = DMF_ParentDeviceGet(DmfModule);
    deviceContext = DeviceContextGet(device);
    reportId = 0;

    switch (ResponseBuffer->ResponseType)
    {
        case OFFER:
            reportId = REPORT_ID_OFFER_INPUT;
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "Sending Offer Response ReportId=0x%x", reportId);
            ntStatus = STATUS_SUCCESS;
            break;
        case CONTENT:
            reportId = REPORT_ID_PAYLOAD_INPUT;
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "Sending Offer Response ReportId=0x%x", reportId);
            ntStatus = STATUS_SUCCESS;
            break;
        default:
            ntStatus = STATUS_UNSUCCESSFUL;
            ASSERT(0);
            break;
    };

    if (NT_SUCCESS(ntStatus))
    {
        HID_XFER_PACKET hidXferPacket;
        RtlZeroMemory(&hidXferPacket, sizeof(hidXferPacket));
        hidXferPacket.reportBuffer = (UCHAR*)&ResponseBuffer->Response;
        hidXferPacket.reportBufferLen = sizeof(ResponseBuffer->Response);
        hidXferPacket.reportId = reportId;

        // This function actually populates the upper layer's input report.
        //
        ntStatus = DMF_VirtualHidDeviceVhf_ReadReportSend(deviceContext->DmfModuleVirtualHidDeviceVhf,
                                                          &hidXferPacket);
        if (! NT_SUCCESS(ntStatus))
        {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "Send Input Report fails ntStatus=0x%x", ntStatus);
        }
    }

    return ntStatus;
}

#pragma code_seg("PAGED")
NTSTATUS
CfuDevice_EvtDevicePrepareHardware(
    _In_  WDFDEVICE Device,
    _In_  WDFCMRESLIST ResourcesRaw,
    _In_  WDFCMRESLIST ResourcesTranslated
    )
/*++

Routine Description:

    Called when device is added so that resources can be assigned.

Arguments:

    Driver - A handle to a framework driver object.
    ResourcesRaw - A handle to a framework resource-list object that identifies the 
                   raw hardware resources that the Plug and Play manager has assigned to the device.
    ResourcesTranslated - A handle to a framework resource-list object that identifies the 
                   translated hardware resources that the Plug and Play manager has assigned to the device.

Return Value:

    NTSTATUS

--*/
{
    NTSTATUS ntStatus;
    PDEVICE_CONTEXT deviceContext;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(ResourcesRaw);
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    FuncEntry(TRACE_DEVICE);

    ntStatus = STATUS_SUCCESS;
    deviceContext = DeviceContextGet(Device);

    // Set up some default values.
    //
    deviceContext->ComponentId = COMPONENT_ID;
    deviceContext->ComponentVersion.MajorVersion = FIRMWARE_VERSION_MAJOR;
    deviceContext->ComponentVersion.MinorVersion = FIRMWARE_VERSION_MINOR;
    deviceContext->ComponentVersion.MinorVersion = FIRMWARE_VERSION_VARIANT;

    // Start the worker thread
    // NOTE: By design, the start/stop of the thread is controlled by the ClientDriver.
    //
    ASSERT(deviceContext->DmfModuleThread != NULL);
    ntStatus = DMF_Thread_Start(deviceContext->DmfModuleThread);
    if (! NT_SUCCESS(ntStatus))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "Worker Thread Start fails: ntStatus=%!STATUS!", ntStatus);
        goto Exit;
    }

Exit:

    FuncExit(TRACE_DEVICE, "ntStatus=%!STATUS!", ntStatus);

    return ntStatus;
}
#pragma code_seg()

#pragma code_seg("PAGED")
NTSTATUS
CfuDevice_EvtDeviceReleaseHardware(
    _In_  WDFDEVICE Device,
    _In_  WDFCMRESLIST ResourcesTranslated
    )
/*++

Routine Description:

    Called when a device is removed.

Arguments:

    Driver - A handle to a framework driver object.
    ResourcesTranslated - A handle to a resource list object that identifies the translated 
                          hardware resources that the Plug and Play manager has assigned to the device.

Return Value:

    NTSTATUS

--*/
{
    PDEVICE_CONTEXT deviceContext;
    NTSTATUS ntStatus = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    PAGED_CODE();

    FuncEntry(TRACE_DEVICE);

    deviceContext = DeviceContextGet(Device);
    ASSERT(deviceContext != NULL);

    // Make sure to close the worker thread.
    //
    ASSERT(deviceContext->DmfModuleThread != NULL);
    DMF_Thread_Stop(deviceContext->DmfModuleThread);

    return ntStatus;
}
#pragma code_seg()