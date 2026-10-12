#include <Uefi.h>

#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseCryptLib.h>
#include <Library/PrintLib.h>
#include <IndustryStandard/Acpi50.h>

#include <Protocol/SimpleFileSystem.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/AcpiSystemDescriptionTable.h>
#include <Protocol/HiiFont.h>
#include <Guid/FileInfo.h>
#include <Protocol/AwSpiNorFlash.h>
#include <Protocol/Http.h>
#include <Protocol/ServiceBinding.h>
#include <Protocol/PxeBaseCode.h>
#include <Protocol/Tcp4.h>
#include <Protocol/Ip4Config2.h>

#define IMAGE_NAME  L"spl-bootable.img"
#define PROGRESS_BAR_HEIGHT  12
#define FLASH_READ_CHUNK     4096
#define STATUS_AREA_HEIGHT   48
#define STATUS_GAP           14
#define WARNING_ICON_SIZE    34
#define RELEASE_API_URL     L"https://api.github.com/repos/OrangePi-Zero3/UEFI-Builder/releases/latest"
#define MAX_HTTP_BODY_SIZE  (16U * 1024U * 1024U)
#define HTTP_CHUNK_SIZE     8192
#define MAX_REDIRECTS       4

typedef enum {
  StatusIconNone,
  StatusIconWarning,
  StatusIconError,
  StatusIconSuccess
} STATUS_ICON;

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL *mGop;
STATIC EFI_HII_FONT_PROTOCOL *mHiiFont;
STATIC UINTN mProgressX;
STATIC UINTN mProgressY;
STATIC UINTN mProgressWidth;
STATIC UINTN mStatusY;
STATIC UINTN mLastProgressPercent = MAX_UINTN;

STATIC
VOID
ShowStatus (
  IN CONST CHAR16 *Text,
  IN STATUS_ICON   Icon
  );

STATIC
BOOLEAN
IsHttpsUrl (
  IN CONST CHAR16  *Url
  );

STATIC
VOID
UpdateProgress (
  IN UINT64 Completed,
  IN UINT64 Total,
  IN UINTN  StartPercent,
  IN UINTN  EndPercent
  );

typedef struct {
  EFI_SERVICE_BINDING_PROTOCOL  *Binding;
  EFI_HTTP_PROTOCOL             *Http;
  EFI_HANDLE                    Controller;
  EFI_HANDLE                    Child;
} HTTP_CLIENT;

STATIC
EFI_STATUS
WaitHttpToken (
  IN EFI_HTTP_PROTOCOL  *Http,
  IN EFI_HTTP_TOKEN     *Token,
  IN EFI_EVENT          Event
  )
{
  EFI_STATUS  Status;
  UINTN       EventIndex;

  Status = gBS->WaitForEvent (1, &Event, &EventIndex);
  if (EFI_ERROR (Status)) {
    Http->Cancel (Http, Token);
    return Status;
  }
  return Token->Status;
}

STATIC
EFI_STATUS
OpenHttpClient (
  OUT HTTP_CLIENT  *Client
  )
{
  EFI_STATUS                    Status;
  EFI_HANDLE                    *Handles;
  EFI_HANDLE                    *PxeHandles;
  EFI_HANDLE                    *MnpHandles;
  EFI_HANDLE                    *ArpHandles;
  EFI_HANDLE                    *Ip4Handles;
  EFI_HANDLE                    *TcpHandles;
  UINTN                         Count;
  UINTN                         PxeCount;
  UINTN                         MnpCount;
  UINTN                         ArpCount;
  UINTN                         Ip4Count;
  UINTN                         TcpCount;
  UINTN                         Index;
  EFI_HTTP_CONFIG_DATA          Config;
  EFI_HTTPv4_ACCESS_POINT       AccessPoint;

  ZeroMem (Client, sizeof (*Client));
  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiPxeBaseCodeProtocolGuid,
                                          NULL, &PxeCount, &PxeHandles))) {
    for (Index = 0; Index < PxeCount; Index++) {
      gBS->ConnectController (PxeHandles[Index], NULL, NULL, TRUE);
    }
    FreePool (PxeHandles);
  }
  // Activate the network interface's MNP and ARP controllers first. Ip4Dxe
  // binds to a controller that exposes both service bindings, then TcpDxe
  // binds to the resulting IPv4 service binding.
  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiManagedNetworkServiceBindingProtocolGuid,
                                          NULL, &MnpCount, &MnpHandles))) {
    DEBUG ((EFI_D_INFO, "Updater found %u MNP service handles\n", (UINT32)MnpCount));
    for (Index = 0; Index < MnpCount; Index++) {
      Status = gBS->ConnectController (MnpHandles[Index], NULL, NULL, TRUE);
      if (EFI_ERROR (Status) && (Status != EFI_ALREADY_STARTED)) {
        DEBUG ((EFI_D_WARN, "Could not connect MNP controller %u: %r\n", (UINT32)Index, Status));
      }
    }
    FreePool (MnpHandles);
  } else {
    DEBUG ((EFI_D_WARN, "No MNP service handles found before IPv4 startup\n"));
  }
  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiArpServiceBindingProtocolGuid,
                                          NULL, &ArpCount, &ArpHandles))) {
    DEBUG ((EFI_D_INFO, "Updater found %u ARP service handles\n", (UINT32)ArpCount));
    for (Index = 0; Index < ArpCount; Index++) {
      Status = gBS->ConnectController (ArpHandles[Index], NULL, NULL, TRUE);
      if (EFI_ERROR (Status) && (Status != EFI_ALREADY_STARTED)) {
        DEBUG ((EFI_D_WARN, "Could not connect ARP controller %u: %r\n", (UINT32)Index, Status));
      }
    }
    FreePool (ArpHandles);
  } else {
    DEBUG ((EFI_D_WARN, "No ARP service handles found before IPv4 startup\n"));
  }
  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiIp4ServiceBindingProtocolGuid,
                                          NULL, &Ip4Count, &Ip4Handles))) {
    DEBUG ((EFI_D_INFO, "Updater found %u IPv4 service handles\n", (UINT32)Ip4Count));
    for (Index = 0; Index < Ip4Count; Index++) {
      Status = gBS->ConnectController (Ip4Handles[Index], NULL, NULL, TRUE);
      if (EFI_ERROR (Status) && (Status != EFI_ALREADY_STARTED)) {
        DEBUG ((EFI_D_WARN, "Could not connect IPv4 controller %u: %r\n", (UINT32)Index, Status));
      }
    }
    FreePool (Ip4Handles);
  } else {
    DEBUG ((EFI_D_WARN, "No IPv4 service handles found before TCP startup\n"));
  }
  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiTcp4ServiceBindingProtocolGuid,
                                          NULL, &TcpCount, &TcpHandles))) {
    DEBUG ((EFI_D_INFO, "Updater found %u TCP4 service handles\n", (UINT32)TcpCount));
    for (Index = 0; Index < TcpCount; Index++) {
      Status = gBS->ConnectController (TcpHandles[Index], NULL, NULL, TRUE);
      if (EFI_ERROR (Status) && (Status != EFI_ALREADY_STARTED)) {
        DEBUG ((EFI_D_WARN, "Could not connect TCP4 controller %u: %r\n", (UINT32)Index, Status));
      }
    }
    FreePool (TcpHandles);
  } else {
    DEBUG ((EFI_D_WARN, "No TCP4 service handles found before HTTP startup\n"));
  }
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiHttpServiceBindingProtocolGuid,
                                    NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_WARN, "HTTP service binding not found after connecting TCP4: %r\n", Status));
    return Status;
  }

  Status = EFI_NOT_FOUND;
  for (Index = 0; Index < Count; Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiHttpServiceBindingProtocolGuid,
                                  (VOID **)&Client->Binding);
    if (EFI_ERROR (Status)) {
      continue;
    }
    Client->Controller = Handles[Index];
    Client->Child = NULL;
    Status = Client->Binding->CreateChild (Client->Binding, &Client->Child);
    if (EFI_ERROR (Status)) {
      continue;
    }
    Status = gBS->HandleProtocol (Client->Child, &gEfiHttpProtocolGuid,
                                  (VOID **)&Client->Http);
    if (EFI_ERROR (Status)) {
      Client->Binding->DestroyChild (Client->Binding, Client->Child);
      Client->Child = NULL;
      continue;
    }

    ZeroMem (&Config, sizeof (Config));
    ZeroMem (&AccessPoint, sizeof (AccessPoint));
    Config.HttpVersion = HttpVersion11;
    Config.TimeOutMillisec = 20000;
    Config.AccessPoint.IPv4Node = &AccessPoint;
    AccessPoint.UseDefaultAddress = TRUE;
    Status = Client->Http->Configure (Client->Http, &Config);
    if (!EFI_ERROR (Status)) {
      break;
    }
    Client->Binding->DestroyChild (Client->Binding, Client->Child);
    Client->Child = NULL;
  }

  FreePool (Handles);
  return Status;
}

STATIC
VOID
CloseHttpClient (
  IN OUT HTTP_CLIENT  *Client
  )
{
  if (Client->Http != NULL) {
    Client->Http->Configure (Client->Http, NULL);
    Client->Http = NULL;
  }
  if ((Client->Binding != NULL) && (Client->Child != NULL)) {
    Client->Binding->DestroyChild (Client->Binding, Client->Child);
    Client->Child = NULL;
  }
  Client->Binding = NULL;
  Client->Controller = NULL;
}

STATIC
EFI_STATUS
DownloadUrl (
  IN OUT HTTP_CLIENT  *Client,
  IN     CHAR16       *Url,
  IN     CONST CHAR8  *Accept,
  IN     UINTN        ProgressStart,
  IN     UINTN        ProgressEnd,
  OUT    UINT8        **Body,
  OUT    UINT32       *BodySize,
  OUT    CHAR16       **Redirect
  )
{
  EFI_STATUS              Status;
  EFI_HTTP_REQUEST_DATA   Request;
  EFI_HTTP_MESSAGE        RequestMessage;
  EFI_HTTP_HEADER         RequestHeaders[4];
  EFI_HTTP_TOKEN          RequestToken;
  EFI_HTTP_RESPONSE_DATA  Response;
  EFI_HTTP_MESSAGE        ResponseMessage;
  EFI_HTTP_TOKEN          ResponseToken;
  EFI_EVENT               Event;
  UINT8                   *Buffer;
  UINTN                   Used;
  UINTN                   Capacity;
  UINTN                   Chunk;
  UINTN                   EventIndex;
  UINTN                   ExpectedLength;
  UINTN                   HeaderIndex;
  UINT64                  ParsedLength;
  CHAR8                   *LengthText;
  CHAR8                   Host[256];
  CHAR16                  *HostStart;
  UINTN                   HostLength;

  *Body = NULL;
  *BodySize = 0;
  *Redirect = NULL;
  HostStart = StrStr (Url, L"://");
  if (HostStart == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  HostStart += 3;
  HostLength = 0;
  while ((HostStart[HostLength] != L'\0') && (HostStart[HostLength] != L'/') &&
         (HostLength < sizeof (Host) - 1)) {
    Host[HostLength] = (CHAR8)HostStart[HostLength];
    HostLength++;
  }
  if (HostLength == 0 || HostLength >= sizeof (Host)) {
    return EFI_INVALID_PARAMETER;
  }
  Host[HostLength] = '\0';
  DEBUG ((EFI_D_WARN, "Updater requesting HTTPS host %a\n", Host));

  ZeroMem (&Request, sizeof (Request));
  ZeroMem (&RequestMessage, sizeof (RequestMessage));
  ZeroMem (&RequestToken, sizeof (RequestToken));
  Request.Method = HttpMethodGet;
  Request.Url = Url;
  RequestHeaders[0].FieldName = "Host";
  RequestHeaders[0].FieldValue = Host;
  RequestHeaders[1].FieldName = "Connection";
  RequestHeaders[1].FieldValue = "close";
  RequestHeaders[2].FieldName = "User-Agent";
  RequestHeaders[2].FieldValue = "Silicium-FirmwareUpdater/1.0";
  RequestHeaders[3].FieldName = "Accept";
  RequestHeaders[3].FieldValue = (CHAR8 *)Accept;
  RequestMessage.Data.Request = &Request;
  RequestMessage.HeaderCount = ARRAY_SIZE (RequestHeaders);
  RequestMessage.Headers = RequestHeaders;
  RequestToken.Message = &RequestMessage;
  // WaitForEvent rejects EVT_NOTIFY_SIGNAL events. This event is signaled by
  // the HTTP driver, so use a plain timer event with no timer armed.
  Status = gBS->CreateEvent (EVT_TIMER, TPL_APPLICATION, NULL, NULL, &Event);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  RequestToken.Event = Event;
  Status = Client->Http->Request (Client->Http, &RequestToken);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_WARN, "HTTP Request() rejected the GET: %r\n", Status));
  } else {
    Status = WaitHttpToken (Client->Http, &RequestToken, Event);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_WARN, "HTTP GET transmit completion failed: %r\n", Status));
    }
  }
  gBS->CloseEvent (Event);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_WARN, "HTTP GET request submission failed: %r\n", Status));
    return Status;
  }

  Capacity = MIN (MAX_HTTP_BODY_SIZE, 1024U * 1024U);
  Buffer = AllocatePool (Capacity);
  if (Buffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  Used = 0;
  ZeroMem (&Response, sizeof (Response));
  ZeroMem (&ResponseMessage, sizeof (ResponseMessage));
  ZeroMem (&ResponseToken, sizeof (ResponseToken));
  Response.StatusCode = HTTP_STATUS_UNSUPPORTED_STATUS;
  ExpectedLength = MAX_UINTN;
  ResponseMessage.Data.Response = &Response;
  ResponseToken.Message = &ResponseMessage;
  Status = gBS->CreateEvent (EVT_TIMER, TPL_APPLICATION, NULL, NULL, &Event);
  if (EFI_ERROR (Status)) {
    FreePool (Buffer);
    return Status;
  }
  ResponseToken.Event = Event;

  do {
    if (Used == Capacity) {
      UINTN  NewCapacity;
      UINT8  *NewBuffer;

      if (Capacity >= MAX_HTTP_BODY_SIZE) {
        Status = EFI_BAD_BUFFER_SIZE;
        break;
      }
      NewCapacity = MIN ((UINTN)MAX_HTTP_BODY_SIZE, Capacity * 2);
      NewBuffer = AllocatePool (NewCapacity);
      if (NewBuffer == NULL) {
        Status = EFI_OUT_OF_RESOURCES;
        break;
      }
      CopyMem (NewBuffer, Buffer, Used);
      FreePool (Buffer);
      Buffer = NewBuffer;
      Capacity = NewCapacity;
    }
    Chunk = MIN ((UINTN)HTTP_CHUNK_SIZE, Capacity - Used);
    if (Chunk == 0) {
      Status = EFI_BAD_BUFFER_SIZE;
      break;
    }
    ResponseMessage.Body = Buffer + Used;
    ResponseMessage.BodyLength = Chunk;
    Status = Client->Http->Response (Client->Http, &ResponseToken);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_WARN, "HTTP response request failed: %r\n", Status));
      break;
    }
    Status = gBS->WaitForEvent (1, &Event, &EventIndex);
    if (EFI_ERROR (Status)) {
      Client->Http->Cancel (Client->Http, &ResponseToken);
      break;
    }
    Status = ResponseToken.Status;
    if ((Status == EFI_HTTP_ERROR) &&
        ((Response.StatusCode == HTTP_STATUS_301_MOVED_PERMANENTLY) ||
         (Response.StatusCode == HTTP_STATUS_302_FOUND) ||
         (Response.StatusCode == HTTP_STATUS_303_SEE_OTHER) ||
         (Response.StatusCode == HTTP_STATUS_307_TEMPORARY_REDIRECT) ||
         (Response.StatusCode == HTTP_STATUS_308_PERMANENT_REDIRECT))) {
      Status = EFI_SUCCESS;
    }
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_WARN, "HTTP response wait failed: %r, code %u\n", Status, Response.StatusCode));
      break;
    }
    if ((Used == 0) && ((Response.StatusCode == HTTP_STATUS_301_MOVED_PERMANENTLY) ||
                        (Response.StatusCode == HTTP_STATUS_302_FOUND) ||
                        (Response.StatusCode == HTTP_STATUS_303_SEE_OTHER) ||
                        (Response.StatusCode == HTTP_STATUS_307_TEMPORARY_REDIRECT) ||
                        (Response.StatusCode == HTTP_STATUS_308_PERMANENT_REDIRECT))) {
      UINTN  HeaderIndex;

      for (HeaderIndex = 0; HeaderIndex < ResponseMessage.HeaderCount; HeaderIndex++) {
        if ((ResponseMessage.Headers[HeaderIndex].FieldName != NULL) &&
            (AsciiStriCmp (ResponseMessage.Headers[HeaderIndex].FieldName, "Location") == 0)) {
          *Redirect = AllocateZeroPool ((AsciiStrLen (ResponseMessage.Headers[HeaderIndex].FieldValue) + 1) * sizeof (CHAR16));
          if (*Redirect != NULL) {
            AsciiStrToUnicodeStrS (ResponseMessage.Headers[HeaderIndex].FieldValue, *Redirect,
                                   AsciiStrLen (ResponseMessage.Headers[HeaderIndex].FieldValue) + 1);
          }
          break;
        }
      }
      if (ResponseMessage.Headers != NULL) {
        FreePool (ResponseMessage.Headers);
        ResponseMessage.Headers = NULL;
        ResponseMessage.HeaderCount = 0;
      }
      Status = (*Redirect != NULL) ? EFI_SUCCESS : EFI_HTTP_ERROR;
      break;
    }
    if ((Used == 0) && (ResponseMessage.Headers != NULL)) {
      for (HeaderIndex = 0; HeaderIndex < ResponseMessage.HeaderCount; HeaderIndex++) {
        if ((ResponseMessage.Headers[HeaderIndex].FieldName != NULL) &&
            (AsciiStriCmp (ResponseMessage.Headers[HeaderIndex].FieldName, "Content-Length") == 0)) {
          LengthText = ResponseMessage.Headers[HeaderIndex].FieldValue;
          ParsedLength = 0;
          while ((*LengthText >= '0') && (*LengthText <= '9')) {
            ParsedLength = (ParsedLength * 10) + (*LengthText++ - '0');
            if (ParsedLength > MAX_HTTP_BODY_SIZE) {
              Status = EFI_BAD_BUFFER_SIZE;
              break;
            }
          }
          if (!EFI_ERROR (Status) && (*LengthText == '\0') && (ParsedLength != 0)) {
            ExpectedLength = (UINTN)ParsedLength;
          } else if (!EFI_ERROR (Status)) {
            Status = EFI_COMPROMISED_DATA;
          }
          break;
        }
      }
      if (EFI_ERROR (Status)) {
        break;
      }
    }
    if ((Used == 0) && (Response.StatusCode != HTTP_STATUS_200_OK)) {
      Status = EFI_HTTP_ERROR;
      break;
    }
    Used += ResponseMessage.BodyLength;
    if ((ProgressEnd > ProgressStart) && (ExpectedLength != MAX_UINTN)) {
      UpdateProgress (Used, ExpectedLength, ProgressStart, ProgressEnd);
    }
    if (ResponseMessage.Headers != NULL) {
      FreePool (ResponseMessage.Headers);
      ResponseMessage.Headers = NULL;
      ResponseMessage.HeaderCount = 0;
      ResponseMessage.Data.Response = NULL;
    }
  } while ((ResponseMessage.BodyLength != 0) && (Used < ExpectedLength));

  gBS->CloseEvent (Event);
  if (ResponseMessage.Headers != NULL) {
    FreePool (ResponseMessage.Headers);
  }
  if (EFI_ERROR (Status)) {
    FreePool (Buffer);
    return Status;
  }
  if (*Redirect != NULL) {
    FreePool (Buffer);
    return EFI_SUCCESS;
  }
  if ((ExpectedLength != MAX_UINTN) && (Used != ExpectedLength)) {
    FreePool (Buffer);
    return EFI_BAD_BUFFER_SIZE;
  }
  *Body = Buffer;
  *BodySize = (UINT32)Used;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
DownloadFollowingRedirects (
  IN OUT HTTP_CLIENT  *Client,
  IN     CHAR16       *InitialUrl,
  IN     CONST CHAR8  *Accept,
  IN     UINTN        ProgressStart,
  IN     UINTN        ProgressEnd,
  OUT    UINT8        **Body,
  OUT    UINT32       *BodySize
  )
{
  EFI_STATUS  Status;
  CHAR16      *Url;
  CHAR16      *Redirect;
  UINTN       Count;

  Url = AllocateCopyPool (StrSize (InitialUrl), InitialUrl);
  if (Url == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  *Body = NULL;
  *BodySize = 0;
  for (Count = 0; Count <= MAX_REDIRECTS; Count++) {
    Redirect = NULL;
    Status = DownloadUrl (Client, Url, Accept, ProgressStart, ProgressEnd,
                          Body, BodySize, &Redirect);
    if (EFI_ERROR (Status) || (Redirect == NULL)) {
      if (Url != InitialUrl) {
        FreePool (Url);
      }
      return Status;
    }
    if (!IsHttpsUrl (Redirect)) {
      FreePool (Redirect);
      FreePool (Url);
      return EFI_SECURITY_VIOLATION;
    }

    // GitHub's asset redirect changes TLS host and SNI. Use a new HTTP/TLS
    // child for each host instead of reconfiguring a closed TLS session.
    CloseHttpClient (Client);
    Status = OpenHttpClient (Client);
    if (EFI_ERROR (Status)) {
      FreePool (Redirect);
      FreePool (Url);
      return Status;
    }

    FreePool (Url);
    Url = Redirect;
  }
  FreePool (Url);
  return EFI_ABORTED;
}

STATIC
CHAR8 *
FindJsonString (
  IN CHAR8  *Json,
  IN CHAR8  *Key,
  IN CHAR8  *Limit
  )
{
  CHAR8  Pattern[64];
  CHAR8  *Found;
  UINTN  Length;

  if (AsciiStrLen (Key) + 3 >= sizeof (Pattern)) {
    return NULL;
  }
  Pattern[0] = '"';
  CopyMem (Pattern + 1, Key, AsciiStrLen (Key));
  Length = AsciiStrLen (Key) + 1;
  Pattern[Length++] = '"';
  Pattern[Length] = '\0';
  Found = Json;
  while ((Found != NULL) && (Found < Limit)) {
    Found = AsciiStrStr (Found, Pattern);
    if ((Found == NULL) || (Found >= Limit)) {
      return NULL;
    }
    Found += Length;
    while ((Found < Limit) && ((*Found == ' ') || (*Found == '\t') || (*Found == '\r') || (*Found == '\n'))) {
      Found++;
    }
    if ((Found < Limit) && (*Found == ':')) {
      Found++;
      while ((Found < Limit) && ((*Found == ' ') || (*Found == '\t') || (*Found == '\r') || (*Found == '\n'))) {
        Found++;
      }
      return Found;
    }
  }
  return NULL;
}

STATIC
EFI_STATUS
ReadJsonString (
  IN CHAR8   *Json,
  IN CHAR8   *Key,
  IN CHAR8   *Limit,
  OUT CHAR8  *Output,
  IN UINTN   OutputSize
  )
{
  CHAR8  *Value;
  UINTN  Used;

  Value = FindJsonString (Json, Key, Limit);
  if ((Value == NULL) || (Value >= Limit) || (*Value != '"')) {
    return EFI_NOT_FOUND;
  }
  Value++;
  Used = 0;
  while ((Value < Limit) && (*Value != '"')) {
    if (*Value == '\\') {
      Value++;
      if (Value >= Limit) {
        return EFI_COMPROMISED_DATA;
      }
      switch (*Value) {
        case 'n': Output[Used++] = '\n'; break;
        case 'r': Output[Used++] = '\r'; break;
        case 't': Output[Used++] = '\t'; break;
        case '"': Output[Used++] = '"'; break;
        case '\\': Output[Used++] = '\\'; break;
        case '/': Output[Used++] = '/'; break;
        default: return EFI_UNSUPPORTED;
      }
    } else {
      if ((UINT8)*Value < 0x20) {
        return EFI_COMPROMISED_DATA;
      }
      Output[Used++] = *Value;
    }
    if (Used + 1 >= OutputSize) {
      return EFI_BUFFER_TOO_SMALL;
    }
    Value++;
  }
  if ((Value >= Limit) || (*Value != '"')) {
    return EFI_COMPROMISED_DATA;
  }
  Output[Used] = '\0';
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ReadJsonNumber (
  IN CHAR8   *Json,
  IN CHAR8   *Key,
  IN CHAR8   *Limit,
  OUT UINT32 *Number
  )
{
  CHAR8  *Value;
  UINT64 Parsed;

  Value = FindJsonString (Json, Key, Limit);
  if ((Value == NULL) || (Value >= Limit) || (*Value < '0') || (*Value > '9')) {
    return EFI_NOT_FOUND;
  }
  Parsed = 0;
  while ((Value < Limit) && (*Value >= '0') && (*Value <= '9')) {
    Parsed = (Parsed * 10) + (*Value++ - '0');
    if (Parsed > MAX_UINT32) {
      return EFI_BAD_BUFFER_SIZE;
    }
  }
  *Number = (UINT32)Parsed;
  return EFI_SUCCESS;
}

STATIC
CHAR8 *
FindJsonObjectEnd (
  IN CHAR8  *ObjectStart,
  IN CHAR8  *Limit
  )
{
  CHAR8    *Cursor;
  UINTN    Depth;
  BOOLEAN  InString;
  BOOLEAN  Escaped;

  if ((ObjectStart >= Limit) || (*ObjectStart != '{')) {
    return NULL;
  }

  Depth   = 0;
  InString = FALSE;
  Escaped  = FALSE;
  for (Cursor = ObjectStart; Cursor < Limit; Cursor++) {
    if (InString) {
      if (Escaped) {
        Escaped = FALSE;
      } else if (*Cursor == '\\') {
        Escaped = TRUE;
      } else if (*Cursor == '"') {
        InString = FALSE;
      }
      continue;
    }

    if (*Cursor == '"') {
      InString = TRUE;
    } else if (*Cursor == '{') {
      Depth++;
    } else if (*Cursor == '}') {
      if (--Depth == 0) {
        return Cursor;
      }
    }
  }

  return NULL;
}

STATIC
INTN
HexDigit (
  IN CHAR8  Value
  )
{
  if ((Value >= '0') && (Value <= '9')) return Value - '0';
  if ((Value >= 'a') && (Value <= 'f')) return Value - 'a' + 10;
  if ((Value >= 'A') && (Value <= 'F')) return Value - 'A' + 10;
  return -1;
}

STATIC
BOOLEAN
IsHttpsUrl (
  IN CONST CHAR16  *Url
  )
{
  return (Url != NULL) &&
         (StrnCmp (Url, L"https://", 8) == 0);
}

STATIC
EFI_STATUS
EnsureDhcp (
  VOID
  )
{
  EFI_STATUS                 Status;
  EFI_HANDLE                 *Handles;
  UINTN                      Count;
  UINTN                      Index;
  UINTN                      Attempt;
  UINTN                      DataSize;
  VOID                       *InterfaceData;
  EFI_IP4_CONFIG2_PROTOCOL   *Ip4Config2;
  EFI_IP4_CONFIG2_POLICY     Policy;
  EFI_IP4_CONFIG2_INTERFACE_INFO *InterfaceInfo;
  EFI_IPv4_ADDRESS           ZeroAddress;

  // PXE's DHCP lease is separate from the IPv4 configuration consumed by
  // TCP/HTTP. Start DHCP through Ip4Config2 so the stack has a source address,
  // gateway, and DNS settings before the HTTP client tries name resolution.
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiIp4Config2ProtocolGuid,
                                   NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = EFI_NOT_FOUND;
  for (Index = 0; Index < Count; Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiIp4Config2ProtocolGuid,
                                  (VOID **)&Ip4Config2);
    if (EFI_ERROR (Status)) {
      continue;
    }

    Policy = Ip4Config2PolicyDhcp;
    Status = Ip4Config2->SetData (Ip4Config2, Ip4Config2DataTypePolicy,
                                  sizeof (Policy), &Policy);
    if (EFI_ERROR (Status) && (Status != EFI_ABORTED)) {
      DEBUG ((EFI_D_WARN, "Could not enable IPv4 DHCP on interface %u: %r\n",
              (UINT32)Index, Status));
      continue;
    }
    Status = EFI_SUCCESS;
  }

  if (EFI_ERROR (Status)) {
    FreePool (Handles);
    return Status;
  }

  ZeroMem (&ZeroAddress, sizeof (ZeroAddress));
  for (Attempt = 0; Attempt < 120; Attempt++) {
    for (Index = 0; Index < Count; Index++) {
      if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiIp4Config2ProtocolGuid,
                                         (VOID **)&Ip4Config2)))
      {
        continue;
      }

      DataSize = 4096;
      InterfaceData = AllocateZeroPool (DataSize);
      if (InterfaceData == NULL) {
        FreePool (Handles);
        return EFI_OUT_OF_RESOURCES;
      }

      Status = Ip4Config2->GetData (Ip4Config2, Ip4Config2DataTypeInterfaceInfo,
                                    &DataSize, InterfaceData);
      if (!EFI_ERROR (Status)) {
        InterfaceInfo = (EFI_IP4_CONFIG2_INTERFACE_INFO *)InterfaceData;
        if (CompareMem (&InterfaceInfo->StationAddress, &ZeroAddress,
                        sizeof (ZeroAddress)) != 0)
        {
          FreePool (InterfaceData);
          FreePool (Handles);
          DEBUG ((EFI_D_INFO, "IPv4 DHCP configured an address on interface %u\n",
                  (UINT32)Index));
          return EFI_SUCCESS;
        }
      }

      FreePool (InterfaceData);
    }

    gBS->Stall (250000);
  }

  FreePool (Handles);
  DEBUG ((EFI_D_WARN, "IPv4 DHCP did not configure an address before timeout\n"));
  return EFI_TIMEOUT;
}

STATIC
EFI_STATUS
FetchOnlineImage (
  OUT UINT8   **Image,
  OUT UINT32   *ImageSize
  )
{
  EFI_STATUS  Status;
  HTTP_CLIENT Client;
  UINT8       *Json;
  UINT32      JsonSize;
  UINT8       *Asset;
  UINT32      AssetSize;
  CHAR16      *Redirect;
  CHAR8       *JsonText;
  CHAR8       *AssetsArray;
  CHAR8       *AssetEntry;
  CHAR8       *AssetEnd;
  CHAR8       *Cursor;
  CHAR8       *Name;
  CHAR8       AssetUrl[2048];
  CHAR8       Digest[80];
  CHAR16      AssetUrl16[2048];
  UINT8       Expected[32];
  UINT8       Actual[32];
  UINT32      DeclaredSize;
  UINTN       Index;
  INTN        High;
  INTN        Low;
  UINTN       RedirectCount;
  BOOLEAN     FoundAsset;

  *Image = NULL;
  *ImageSize = 0;
  Status = EnsureDhcp ();
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_WARN, "Updater DHCP failed: %r\n", Status));
    return Status;
  }
  Status = OpenHttpClient (&Client);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_WARN, "Updater could not open HTTP client: %r\n", Status));
    return Status;
  }
  Json = NULL;
  JsonSize = 0;
  Status = DownloadFollowingRedirects (&Client, RELEASE_API_URL,
                                       "application/vnd.github+json", 0, 0,
                                       &Json, &JsonSize);
  if (EFI_ERROR (Status) || (JsonSize == 0) || (JsonSize >= MAX_HTTP_BODY_SIZE)) {
    DEBUG ((EFI_D_WARN, "Latest release request failed: %r (size %u)\n", Status, JsonSize));
    goto Done;
  }
  JsonText = AllocatePool (JsonSize + 1);
  if (JsonText == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }
  CopyMem (JsonText, Json, JsonSize);
  JsonText[JsonSize] = '\0';
  AssetsArray = FindJsonString (JsonText, "assets", JsonText + JsonSize);
  if ((AssetsArray == NULL) || (AssetsArray >= JsonText + JsonSize) || (*AssetsArray != '[')) {
    Status = EFI_COMPROMISED_DATA;
    FreePool (JsonText);
    goto Done;
  }

  AssetEntry = NULL;
  AssetEnd = NULL;
  FoundAsset = FALSE;
  for (Cursor = AssetsArray + 1; (Cursor < JsonText + JsonSize) && (*Cursor != ']'); Cursor++) {
    if (*Cursor != '{') {
      continue;
    }

    AssetEnd = FindJsonObjectEnd (Cursor, JsonText + JsonSize);
    if (AssetEnd == NULL) {
      Status = EFI_COMPROMISED_DATA;
      FreePool (JsonText);
      goto Done;
    }

    Name = AllocateZeroPool (128);
    if (Name == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      FreePool (JsonText);
      goto Done;
    }
    Status = ReadJsonString (Cursor, "name", AssetEnd, Name, 128);
    if (!EFI_ERROR (Status) && (AsciiStrCmp (Name, "spl-bootable.img") == 0)) {
      FreePool (Name);
      AssetEntry = Cursor;
      FoundAsset = TRUE;
      break;
    }
    FreePool (Name);
    Cursor = AssetEnd;
  }

  if (!FoundAsset) {
    Status = EFI_NOT_FOUND;
    DEBUG ((EFI_D_WARN, "Latest release has no %s asset\n", IMAGE_NAME));
    FreePool (JsonText);
    goto Done;
  }
  Status = ReadJsonNumber (AssetEntry, "size", AssetEnd, &DeclaredSize);
  if (EFI_ERROR (Status) || (DeclaredSize == 0) || (DeclaredSize > MAX_HTTP_BODY_SIZE)) {
    Status = EFI_BAD_BUFFER_SIZE;
    FreePool (JsonText);
    goto Done;
  }
  Status = ReadJsonString (AssetEntry, "digest", AssetEnd, Digest, sizeof (Digest));
  if (EFI_ERROR (Status) || (AsciiStrLen (Digest) != 71) || (AsciiStrnCmp (Digest, "sha256:", 7) != 0)) {
    Status = EFI_COMPROMISED_DATA;
    FreePool (JsonText);
    goto Done;
  }
  for (Index = 0; Index < 32; Index++) {
    High = HexDigit (Digest[7 + 2 * Index]);
    Low = HexDigit (Digest[8 + 2 * Index]);
    if ((High < 0) || (Low < 0)) {
      Status = EFI_COMPROMISED_DATA;
      FreePool (JsonText);
      goto Done;
    }
    Expected[Index] = (UINT8)((High << 4) | Low);
  }
  // The API asset endpoint can serve the binary with an octet-stream Accept
  // header, avoiding the separate github.com browser-download host.
  Status = ReadJsonString (AssetEntry, "url", AssetEnd, AssetUrl, sizeof (AssetUrl));
  if (EFI_ERROR (Status) || (AsciiStrLen (AssetUrl) >= ARRAY_SIZE (AssetUrl16))) {
    Status = EFI_COMPROMISED_DATA;
    FreePool (JsonText);
    goto Done;
  }
  AsciiStrToUnicodeStrS (AssetUrl, AssetUrl16, ARRAY_SIZE (AssetUrl16));
  if (!IsHttpsUrl (AssetUrl16)) {
    Status = EFI_SECURITY_VIOLATION;
    FreePool (JsonText);
    goto Done;
  }

  for (Index = 5; Index > 0; Index--) {
    CHAR16 CountdownText[48];
    UnicodeSPrint (CountdownText, sizeof (CountdownText), L"Downloading update in %u seconds", (UINT32)Index);
    ShowStatus (CountdownText, StatusIconWarning);
    gBS->Stall (1000000);
  }
  FreePool (JsonText);

  // The metadata server closes its connection. Use a fresh HTTP/TLS child for
  // the binary request rather than reconfiguring the closed TLS session.
  CloseHttpClient (&Client);
  Status = OpenHttpClient (&Client);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_WARN, "Could not reopen HTTP client for firmware asset: %r\n", Status));
    goto Done;
  }

  Asset = NULL;
  AssetSize = 0;
  ShowStatus (L"Downloading latest firmware", StatusIconWarning);
  {
    CHAR16 *CurrentUrl;
    CurrentUrl = AllocateCopyPool (StrSize (AssetUrl16), AssetUrl16);
    if (CurrentUrl == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      goto Done;
    }
    for (RedirectCount = 0; RedirectCount <= MAX_REDIRECTS; RedirectCount++) {
      Redirect = NULL;
      Status = DownloadUrl (&Client, CurrentUrl, "application/octet-stream", 0, 15,
                            &Asset, &AssetSize, &Redirect);
      if (EFI_ERROR (Status) || (Redirect == NULL)) {
        FreePool (CurrentUrl);
        break;
      }
      if (!IsHttpsUrl (Redirect)) {
        FreePool (Redirect);
        FreePool (CurrentUrl);
        Status = EFI_SECURITY_VIOLATION;
        break;
      }

      // The redirect points at a different TLS host. Recreate the HTTP/TLS
      // child so the next request starts with a clean session and SNI name.
      CloseHttpClient (&Client);
      Status = OpenHttpClient (&Client);
      if (EFI_ERROR (Status)) {
        FreePool (Redirect);
        FreePool (CurrentUrl);
        break;
      }

      FreePool (CurrentUrl);
      CurrentUrl = Redirect;
    }
    if (RedirectCount > MAX_REDIRECTS) {
      FreePool (CurrentUrl);
      Status = EFI_ABORTED;
    }
  }
  if (EFI_ERROR (Status)) {
    if (Asset != NULL) FreePool (Asset);
    goto Done;
  }
  if ((AssetSize != DeclaredSize) || !Sha256HashAll (Asset, AssetSize, Actual) ||
      (CompareMem (Expected, Actual, sizeof (Expected)) != 0)) {
    FreePool (Asset);
    Status = EFI_SECURITY_VIOLATION;
    goto Done;
  }
  *Image = Asset;
  *ImageSize = AssetSize;
  Status = EFI_SUCCESS;

Done:
  if (Json != NULL) {
    FreePool (Json);
  }
  CloseHttpClient (&Client);
  return Status;
}

STATIC
VOID
FillRectangle (
  IN UINTN X,
  IN UINTN Y,
  IN UINTN Width,
  IN UINTN Height,
  IN UINT8 Red,
  IN UINT8 Green,
  IN UINT8 Blue
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Color;

  if (mGop == NULL) {
    return;
  }

  Color.Red = Red;
  Color.Green = Green;
  Color.Blue = Blue;
  Color.Reserved = 0;
  mGop->Blt (mGop, &Color, EfiBltVideoFill, 0, 0, X, Y, Width, Height, 0);
}

STATIC
VOID
DrawWarningIcon (
  IN UINTN X,
  IN UINTN Y
  )
{
  UINTN Row;
  UINTN Inset;

  // A compact yellow warning triangle with a dark exclamation mark.
  for (Row = 0; Row < WARNING_ICON_SIZE; Row++) {
    Inset = (WARNING_ICON_SIZE - (Row + 1)) / 2;
    FillRectangle (X + Inset, Y + Row, WARNING_ICON_SIZE - (2 * Inset), 1,
                   0xFF, 0xC8, 0x24);
  }

  FillRectangle (X + 15, Y + 10, 4, 12, 0x30, 0x28, 0x18);
  FillRectangle (X + 15, Y + 25, 4, 4, 0x30, 0x28, 0x18);
}

STATIC
VOID
DrawRoundIcon (
  IN UINTN X,
  IN UINTN Y,
  IN UINT8 Red,
  IN UINT8 Green,
  IN UINT8 Blue
  )
{
  INTN Row;
  INTN Col;
  INTN Center;
  INTN Radius;
  INTN HalfWidth;

  Center = (INTN)(WARNING_ICON_SIZE / 2);
  Radius = Center - 1;
  for (Row = -Radius; Row <= Radius; Row++) {
    HalfWidth = 0;
    for (Col = 0; Col <= Radius; Col++) {
      if ((Col * Col) + (Row * Row) > (Radius * Radius)) {
        break;
      }
      HalfWidth = Col;
    }
    FillRectangle (X + Center - HalfWidth, Y + Center + Row,
                   (UINTN)(2 * HalfWidth + 1), 1, Red, Green, Blue);
  }
}

STATIC
VOID
DrawErrorIcon (
  IN UINTN X,
  IN UINTN Y
  )
{
  UINTN Step;

  DrawRoundIcon (X, Y, 0xE8, 0x42, 0x42);
  // Keep the stroke endpoints well inside the circle so the X does not touch
  // its lower-left and lower-right edges.
  for (Step = 10; Step < 23; Step++) {
    FillRectangle (X + Step, Y + Step, 4, 4, 0xFF, 0xFF, 0xFF);
    FillRectangle (X + WARNING_ICON_SIZE - Step - 4, Y + Step, 4, 4,
                   0xFF, 0xFF, 0xFF);
  }
}

STATIC
VOID
DrawSuccessIcon (
  IN UINTN X,
  IN UINTN Y
  )
{
  UINTN Step;

  DrawRoundIcon (X, Y, 0x42, 0xC8, 0x74);
  for (Step = 0; Step < 8; Step++) {
    FillRectangle (X + 6 + Step, Y + 14 + Step, 4, 4, 0x18, 0x30, 0x20);
  }
  for (Step = 0; Step < 13; Step++) {
    FillRectangle (X + 12 + Step, Y + 22 - Step, 4, 4, 0x18, 0x30, 0x20);
  }
}

STATIC
VOID
ShowStatus (
  IN CONST CHAR16 *Text,
  IN STATUS_ICON   Icon
  )
{
  EFI_IMAGE_OUTPUT       Image;
  EFI_IMAGE_OUTPUT      *ImagePointer;
  EFI_FONT_DISPLAY_INFO  DisplayInfo;
  EFI_HII_ROW_INFO      *RowInfo;
  EFI_STATUS             Status;
  UINTN                  RowInfoCount;
  UINTN                  TextWidth;
  UINTN                  TextHeight;
  UINTN                  TextX;
  UINTN                  TextY;
  UINTN                  GroupWidth;
  UINTN                  IconX;
  BOOLEAN                DrawIcon;

  if ((mGop == NULL) || (mHiiFont == NULL) || (mProgressWidth == 0)) {
    return;
  }

  Image.Width = (UINT16)mProgressWidth;
  Image.Height = STATUS_AREA_HEIGHT;
  Image.Image.Bitmap = AllocateZeroPool (
                         Image.Width * Image.Height * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
                         );
  if (Image.Image.Bitmap == NULL) {
    return;
  }

  ImagePointer = &Image;
  ZeroMem (&DisplayInfo, sizeof (DisplayInfo));
  DisplayInfo.ForegroundColor.Red = 0xF0;
  DisplayInfo.ForegroundColor.Green = 0xF0;
  DisplayInfo.ForegroundColor.Blue = 0xF0;
  DisplayInfo.BackgroundColor.Red = 0;
  DisplayInfo.BackgroundColor.Green = 0;
  DisplayInfo.BackgroundColor.Blue = 0;
  DisplayInfo.FontInfoMask = EFI_FONT_INFO_SYS_FONT | EFI_FONT_INFO_SYS_SIZE | EFI_FONT_INFO_SYS_STYLE;
  DisplayInfo.FontInfo.FontName[0] = L'\0';

  RowInfo = NULL;
  RowInfoCount = 0;
  Status = mHiiFont->StringToImage (mHiiFont, EFI_HII_OUT_FLAG_CLIP,
                                    (EFI_STRING)Text, &DisplayInfo, &ImagePointer,
                                    0, 0, &RowInfo, &RowInfoCount, NULL);
  if (EFI_ERROR (Status) || (RowInfoCount == 0)) {
    if (RowInfo != NULL) {
      FreePool (RowInfo);
    }
    FreePool (Image.Image.Bitmap);
    return;
  }

  TextWidth = RowInfo[0].LineWidth;
  TextHeight = RowInfo[0].LineHeight;
  FreePool (RowInfo);

  DrawIcon = (Icon != StatusIconNone) && (mProgressWidth >= 180) &&
             (TextWidth + WARNING_ICON_SIZE + 12 <= mProgressWidth);
  if (DrawIcon) {
    GroupWidth = WARNING_ICON_SIZE + 12 + TextWidth;
    IconX = (mProgressWidth - GroupWidth) / 2;
    TextX = IconX + WARNING_ICON_SIZE + 12;
  } else {
    IconX = 0;
    TextX = (mProgressWidth > TextWidth) ? (mProgressWidth - TextWidth) / 2 : 0;
  }
  TextY = (STATUS_AREA_HEIGHT > TextHeight) ? (STATUS_AREA_HEIGHT - TextHeight) / 2 : 0;

  ZeroMem (Image.Image.Bitmap,
           Image.Width * Image.Height * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
  Status = mHiiFont->StringToImage (mHiiFont, EFI_HII_OUT_FLAG_CLIP,
                                    (EFI_STRING)Text, &DisplayInfo, &ImagePointer,
                                    TextX, TextY, NULL, NULL, NULL);
  if (!EFI_ERROR (Status)) {
    mGop->Blt (mGop, Image.Image.Bitmap, EfiBltBufferToVideo, 0, 0,
               mProgressX, mStatusY, Image.Width, Image.Height, 0);
    if (DrawIcon) {
      if (Icon == StatusIconWarning) {
        DrawWarningIcon (mProgressX + IconX, mStatusY + (STATUS_AREA_HEIGHT - WARNING_ICON_SIZE) / 2);
      } else if (Icon == StatusIconError) {
        DrawErrorIcon (mProgressX + IconX, mStatusY + (STATUS_AREA_HEIGHT - WARNING_ICON_SIZE) / 2);
      } else {
        DrawSuccessIcon (mProgressX + IconX, mStatusY + (STATUS_AREA_HEIGHT - WARNING_ICON_SIZE) / 2);
      }
    }
  }
  FreePool (Image.Image.Bitmap);
}

STATIC
VOID
DrawProgress (
  IN UINTN Percent
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color;
  UINTN                          InnerWidth;
  UINTN                          FillWidth;

  if ((mGop == NULL) || (mProgressWidth == 0)) {
    return;
  }

  if (Percent > 100) {
    Percent = 100;
  }

  Color.Blue = 0xB0;
  Color.Green = 0xB0;
  Color.Red = 0xB0;
  Color.Reserved = 0;
  mGop->Blt (mGop, &Color, EfiBltVideoFill, 0, 0, mProgressX, mProgressY,
             mProgressWidth, PROGRESS_BAR_HEIGHT, 0);

  InnerWidth = mProgressWidth > 4 ? mProgressWidth - 4 : 0;
  Color.Blue = 0x20;
  Color.Green = 0x20;
  Color.Red = 0x20;
  mGop->Blt (mGop, &Color, EfiBltVideoFill, 0, 0, mProgressX + 2,
             mProgressY + 2, InnerWidth, PROGRESS_BAR_HEIGHT - 4, 0);

  FillWidth = (InnerWidth * Percent) / 100;
  if (FillWidth != 0) {
    Color.Blue = 0xD0;
    Color.Green = 0xA0;
    Color.Red = 0x30;
    mGop->Blt (mGop, &Color, EfiBltVideoFill, 0, 0, mProgressX + 2,
               mProgressY + 2, FillWidth, PROGRESS_BAR_HEIGHT - 4, 0);
  }
}

STATIC
VOID
UpdateProgress (
  IN UINT64 Completed,
  IN UINT64 Total,
  IN UINTN  StartPercent,
  IN UINTN  EndPercent
  )
{
  UINTN Percent;

  if ((Total == 0) || (EndPercent < StartPercent)) {
    return;
  }

  if (Completed > Total) {
    Completed = Total;
  }

  Percent = StartPercent + (UINTN)(((UINT64)(EndPercent - StartPercent) * Completed) / Total);
  if (Percent != mLastProgressPercent) {
    DrawProgress (Percent);
    mLastProgressPercent = Percent;
  }
}

// Position the bar just below the BGRT bitmap, leaving the logo untouched.
STATIC
VOID
InitializeProgressBar (
  VOID
  )
{
  EFI_ACPI_SDT_PROTOCOL                     *AcpiSdt;
  EFI_ACPI_SDT_HEADER                       *AcpiTable;
  EFI_ACPI_5_0_BOOT_GRAPHICS_RESOURCE_TABLE  *Bgrt;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION      *Mode;
  UINTN                                      Index;
  UINTN                                      TableKey;
  UINTN                                      ScreenWidth;
  UINTN                                      ScreenHeight;
  UINTN                                      LogoBottom;
  UINT32                                     LogoWidth;
  UINT32                                     LogoHeight;
  UINT32                                     BmpWidth;
  UINT32                                     BmpHeight;
  EFI_ACPI_TABLE_VERSION                    Version;

  mGop = NULL;
  mHiiFont = NULL;
  mProgressWidth = 0;
  if (EFI_ERROR (gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&mGop)) ||
      (mGop->Mode == NULL) || (mGop->Mode->Info == NULL)) {
    mGop = NULL;
    return;
  }

  gBS->LocateProtocol (&gEfiHiiFontProtocolGuid, NULL, (VOID **)&mHiiFont);

  Mode = mGop->Mode->Info;
  ScreenWidth = Mode->HorizontalResolution;
  ScreenHeight = Mode->VerticalResolution;
  mProgressWidth = MIN (ScreenWidth > 40 ? ScreenWidth - 40 : 0, 480);
  if (mProgressWidth < 80) {
    mProgressWidth = 0;
    return;
  }

  mProgressX = (ScreenWidth - mProgressWidth) / 2;
  LogoBottom = 0;
  LogoWidth = 0;
  LogoHeight = 0;
  if (!EFI_ERROR (gBS->LocateProtocol (&gEfiAcpiSdtProtocolGuid, NULL, (VOID **)&AcpiSdt))) {
    for (Index = 0; ; Index++) {
      if (EFI_ERROR (AcpiSdt->GetAcpiTable (Index, &AcpiTable, &Version, &TableKey))) {
        break;
      }

      if ((AcpiTable->Signature == EFI_ACPI_5_0_BOOT_GRAPHICS_RESOURCE_TABLE_SIGNATURE) &&
          (AcpiTable->Length >= sizeof (*Bgrt))) {
        Bgrt = (EFI_ACPI_5_0_BOOT_GRAPHICS_RESOURCE_TABLE *)AcpiTable;
        if (((Bgrt->Status & EFI_ACPI_5_0_BGRT_STATUS_VALID) != 0) &&
            (Bgrt->ImageType == EFI_ACPI_5_0_BGRT_IMAGE_TYPE_BMP) &&
            (Bgrt->ImageAddress != 0)) {
          // BITMAPINFOHEADER width/height are at byte offsets 18 and 22.
          // Use the absolute height because BMPs may be stored bottom-up.
          CopyMem (&BmpWidth, (VOID *)(UINTN)(Bgrt->ImageAddress + 18), sizeof (BmpWidth));
          CopyMem (&BmpHeight, (VOID *)(UINTN)(Bgrt->ImageAddress + 22), sizeof (BmpHeight));
          LogoWidth = BmpWidth;
          LogoHeight = BmpHeight;
          if ((BmpHeight & 0x80000000U) != 0) {
            LogoHeight = (UINT32)(-(INT32)BmpHeight);
          }
          if ((LogoWidth != 0) && (LogoHeight != 0) && (LogoWidth <= ScreenWidth) &&
              (LogoHeight <= ScreenHeight) && (Bgrt->ImageOffsetX <= ScreenWidth - LogoWidth) &&
              (Bgrt->ImageOffsetY <= ScreenHeight - LogoHeight)) {
            LogoBottom = Bgrt->ImageOffsetY + LogoHeight;
            mProgressWidth = MIN (mProgressWidth, LogoWidth);
            mProgressX = Bgrt->ImageOffsetX + (LogoWidth - mProgressWidth) / 2;
          }
        }
        break;
      }
    }
  }

  // Reserve room below the bar for a status line.
  mProgressY = (LogoBottom != 0) ? LogoBottom + 18 : (ScreenHeight * 3) / 4;
  if ((LogoBottom != 0) &&
      (mProgressY + PROGRESS_BAR_HEIGHT + STATUS_GAP + STATUS_AREA_HEIGHT > ScreenHeight)) {
    mProgressWidth = 0;
    return;
  }
  if (LogoBottom == 0) {
    if (ScreenHeight <= (PROGRESS_BAR_HEIGHT + STATUS_GAP + STATUS_AREA_HEIGHT + 8)) {
      mProgressWidth = 0;
      return;
    }
    mProgressY = MIN (mProgressY, ScreenHeight - PROGRESS_BAR_HEIGHT - STATUS_GAP - STATUS_AREA_HEIGHT - 8);
    mProgressX = (ScreenWidth - mProgressWidth) / 2;
  }

  mStatusY = mProgressY + PROGRESS_BAR_HEIGHT + STATUS_GAP;
  DrawProgress (0);
  mLastProgressPercent = 0;
}

STATIC
EFI_STATUS
ReadWholeFile (
  IN  EFI_FILE_PROTOCOL  *File,
  OUT UINT8             **Buffer,
  OUT UINT32             *Size
  )
{
  EFI_STATUS     Status;
  EFI_FILE_INFO  *Info;
  UINTN          InfoSize;
  UINT64         FileSize;
  UINTN          ReadSize;
  UINT8          *Buf;

  InfoSize = 0;
  Info     = NULL;
  Status   = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return EFI_DEVICE_ERROR;
  }

  Info = AllocatePool (InfoSize);
  if (Info == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, Info);
  if (EFI_ERROR (Status)) {
    FreePool (Info);
    return Status;
  }

  FileSize = Info->FileSize;
  FreePool (Info);

  if ((FileSize == 0) || (FileSize > MAX_UINT32)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  Buf = AllocatePool ((UINTN)FileSize);
  if (Buf == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  ReadSize = (UINTN)FileSize;
  Status   = File->Read (File, &ReadSize, Buf);
  if (EFI_ERROR (Status) || (ReadSize != (UINTN)FileSize)) {
    FreePool (Buf);
    return EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
  }

  *Buffer = Buf;
  *Size   = (UINT32)FileSize;
  return EFI_SUCCESS;
}

//
// Look at the root of every filesystem volume, take the first image found.
//
STATIC
EFI_STATUS
LoadImageFromVolumes (
  OUT UINT8   **Image,
  OUT UINT32   *ImageSize
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles;
  UINTN       HandleCount;
  UINTN       Index;
  EFI_STATUS  Result;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleFileSystemProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }

  Result = EFI_NOT_FOUND;

  for (Index = 0; Index < HandleCount; Index++) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
    EFI_FILE_PROTOCOL                *Root;
    EFI_FILE_PROTOCOL                *File;

    Status = gBS->HandleProtocol (Handles[Index], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
    if (EFI_ERROR (Status)) {
      continue;
    }

    Status = Fs->OpenVolume (Fs, &Root);
    if (EFI_ERROR (Status)) {
      continue;
    }

    Status = Root->Open (Root, &File, IMAGE_NAME, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR (Status)) {
      Root->Close (Root);
      continue;
    }

    Status = ReadWholeFile (File, Image, ImageSize);
    File->Close (File);
    Root->Close (Root);

    if (!EFI_ERROR (Status)) {
      Result = EFI_SUCCESS;
      break;
    }

    DEBUG ((EFI_D_ERROR, "Failed to read %s on volume %lu: %r\n", IMAGE_NAME, (UINT64)Index, Status));
  }

  FreePool (Handles);
  return Result;
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS              Status;
  AW_SPI_NOR_FLASH_PROTOCOL  *Nor;
  UINT8                   *Image;
  UINT8                   *Current;
  UINT32                  ImageSize;
  UINT32                  EraseLen;
  UINT32                  EraseOffset;
  UINT32                  WriteOffset;
  UINT32                  ChunkSize;
  UINT32                  ReadOffset;
  UINT64                  EraseLen64;
  UINTN                   Countdown;
  BOOLEAN                 LocalImage;

  Image     = NULL;
  Current   = NULL;
  ImageSize = 0;

  Status = LoadImageFromVolumes (&Image, &ImageSize);
  if (Status == EFI_NOT_FOUND) {
    LocalImage = FALSE;
    InitializeProgressBar ();
    ShowStatus (L"No offline image; checking online release", StatusIconWarning);
    DEBUG ((EFI_D_WARN, "No local %s found; checking online release\n", IMAGE_NAME));
    Status = FetchOnlineImage (&Image, &ImageSize);
    if (EFI_ERROR (Status)) {
      ShowStatus (L"Online update unavailable", StatusIconError);
      DEBUG ((EFI_D_WARN, "Online update unavailable: %r\n", Status));
      gBS->Stall (3000000);
      return EFI_SUCCESS;
    }
  } else {
    LocalImage = TRUE;
  }

  if (EFI_ERROR (Status)) {
    InitializeProgressBar ();
    ShowStatus (L"Unable to load firmware image", StatusIconError);
    DEBUG ((EFI_D_ERROR, "Failed to load %s: %r\n", IMAGE_NAME, Status));
    return Status;
  }

  InitializeProgressBar ();
  ShowStatus (LocalImage ? L"Checking offline firmware image" : L"Checking downloaded firmware image",
              StatusIconNone);

  Status = gBS->LocateProtocol (&gAwSpiNorFlashProtocolGuid, NULL, (VOID **)&Nor);
  if (EFI_ERROR (Status)) {
    ShowStatus (L"SPI flash is unavailable", StatusIconError);
    DEBUG ((EFI_D_ERROR, "SPI NOR protocol not available: %r\n", Status));
    goto Out;
  }

  if ((Nor->EraseSize == 0) || (Nor->PageSize == 0)) {
    ShowStatus (L"Invalid SPI flash geometry", StatusIconError);
    DEBUG ((EFI_D_ERROR, "SPI NOR reports invalid erase or page size\n"));
    Status = EFI_DEVICE_ERROR;
    goto Out;
  }

  EraseLen64 = (((UINT64)ImageSize + Nor->EraseSize - 1) / Nor->EraseSize) * Nor->EraseSize;
  if ((EraseLen64 > MAX_UINT32) || (EraseLen64 > Nor->FlashSize)) {
    ShowStatus (L"Firmware image exceeds flash capacity", StatusIconError);
    DEBUG ((EFI_D_ERROR, "Image (%u bytes) does not fit in flash (%lu bytes)\n", ImageSize, Nor->FlashSize));
    Status = EFI_BAD_BUFFER_SIZE;
    goto Out;
  }
  EraseLen = (UINT32)EraseLen64;

  if ((ImageSize < 12) || (CompareMem (Image + 4, "eGON.BT0", 8) != 0)) {
    ShowStatus (L"Firmware image signature is invalid", StatusIconError);
    DEBUG ((EFI_D_ERROR, "Image does not have valid SPL signature, aborting\n"));
    Status = EFI_ABORTED;
    goto Out;
  }

  Current = AllocatePool (ImageSize);
  if (Current == NULL) {
    ShowStatus (L"Not enough memory to continue", StatusIconError);
    Status = EFI_OUT_OF_RESOURCES;
    goto Out;
  }

  mLastProgressPercent = 0;
  ShowStatus (L"Reading current firmware", StatusIconNone);
  for (ReadOffset = 0; ReadOffset < ImageSize; ReadOffset += ChunkSize) {
    ChunkSize = MIN (FLASH_READ_CHUNK, ImageSize - ReadOffset);
    Status = Nor->Read (Nor, ReadOffset, ChunkSize, Current + ReadOffset);
    if (EFI_ERROR (Status)) {
      ShowStatus (L"Flash read failed", StatusIconError);
      DEBUG ((EFI_D_ERROR, "Flash read failed: %r\n", Status));
      goto Out;
    }

    UpdateProgress ((UINT64)ReadOffset + ChunkSize, ImageSize, 0, 10);
  }

  if (CompareMem (Image, Current, ImageSize) == 0) {
    DrawProgress (100);
    mLastProgressPercent = 100;
    ShowStatus (L"Firmware is already up to date", StatusIconSuccess);
    DEBUG ((EFI_D_WARN, "Flash already contains this image, nothing to do\n"));
    Status = EFI_SUCCESS;
    goto Out;
  }

  ShowStatus (L"Updating firmware - do not power off", StatusIconWarning);
  DEBUG ((EFI_D_WARN, "Flash differs, updating (erase %u bytes)... do not power off\n", EraseLen));
  ShowStatus (L"Erasing flash - do not power off", StatusIconWarning);
  for (EraseOffset = 0; EraseOffset < EraseLen; EraseOffset += Nor->EraseSize) {
    Status = Nor->Erase (Nor, EraseOffset, Nor->EraseSize);
    if (EFI_ERROR (Status)) {
      ShowStatus (L"Flash erase failed", StatusIconError);
      DEBUG ((EFI_D_ERROR, "Flash erase failed at 0x%08x: %r\n", EraseOffset, Status));
      goto Out;
    }

    UpdateProgress ((UINT64)EraseOffset + Nor->EraseSize, EraseLen, 10, 40);
  }

  ShowStatus (L"Writing firmware - do not power off", StatusIconWarning);
  for (WriteOffset = 0; WriteOffset < ImageSize; WriteOffset += ChunkSize) {
    ChunkSize = MIN (Nor->PageSize, ImageSize - WriteOffset);
    Status = Nor->Write (Nor, WriteOffset, ChunkSize, Image + WriteOffset);
    if (EFI_ERROR (Status)) {
      ShowStatus (L"Flash write failed", StatusIconError);
      DEBUG ((EFI_D_ERROR, "Flash write failed at 0x%08x: %r\n", WriteOffset, Status));
      goto Out;
    }

    UpdateProgress ((UINT64)WriteOffset + ChunkSize, ImageSize, 40, 75);
  }

  ShowStatus (L"Verifying firmware - do not power off", StatusIconWarning);
  for (ReadOffset = 0; ReadOffset < ImageSize; ReadOffset += ChunkSize) {
    ChunkSize = MIN (FLASH_READ_CHUNK, ImageSize - ReadOffset);
    Status = Nor->Read (Nor, ReadOffset, ChunkSize, Current + ReadOffset);
    if (EFI_ERROR (Status)) {
      ShowStatus (L"Flash verify read failed", StatusIconError);
      DEBUG ((EFI_D_ERROR, "Flash verify read failed at 0x%08x: %r\n", ReadOffset, Status));
      goto Out;
    }

    if (CompareMem (Image + ReadOffset, Current + ReadOffset, ChunkSize) != 0) {
      ShowStatus (L"Verification failed - do not reboot", StatusIconError);
      DEBUG ((EFI_D_ERROR, "VERIFY FAILED at flash offset 0x%08x!\n", ReadOffset));
      Status = EFI_DEVICE_ERROR;
      goto Out;
    }

    UpdateProgress ((UINT64)ReadOffset + ChunkSize, ImageSize, 75, 100);
  }

  DrawProgress (100);
  ShowStatus (L"Update verified - restarting shortly", StatusIconSuccess);
  DEBUG ((EFI_D_WARN, "Flash updated and verified, rebooting...\n"));
  for (Countdown = 5; Countdown > 0; Countdown--) {
    switch (Countdown) {
      case 5: ShowStatus (L"Restarting in 5 seconds", StatusIconSuccess); break;
      case 4: ShowStatus (L"Restarting in 4 seconds", StatusIconSuccess); break;
      case 3: ShowStatus (L"Restarting in 3 seconds", StatusIconSuccess); break;
      case 2: ShowStatus (L"Restarting in 2 seconds", StatusIconSuccess); break;
      case 1: ShowStatus (L"Restarting in 1 second", StatusIconSuccess); break;
    }
    gBS->Stall (1000000);
  }
  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
  Status = EFI_SUCCESS;

Out:
  if (Image != NULL) {
    FreePool (Image);
  }

  if (Current != NULL) {
    FreePool (Current);
  }

  return Status;
}
