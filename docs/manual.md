## Basicfeature

Power

DC5V~DC24V

Temperature MeasuringRange

-30°℃~80℃

HumidityMeasuring Range

0~100%RH

Measuring Precision

Temperature:±0.5°C (resolution:0.1°C)

Humidity:±5%rh (resolution: 0.1rh)

Output

RS485(ProtocoIMODBUSRTU)

Consumption

&lt;0.1W

RS485Communication

up to 2000m

distance

Defaultcablelength

1M (can customize length)

## SENSORWIRING

| Color   | Illustrate   |
|---------|--------------|
| Red     | VCC          |
| Black   | GND          |
| Yellow  | 485-A        |

## Green ZElectronic 485-B

## CONFIGURATIONSOFTWAREINSTALLATIONANDUSE

WEPROVIDEAMATCHING"LY485-TOOL",WHICHCANEASILYUSEACOMPUTERTOREADTHEPARAMETERSOFTHESENSOR, ANDFLEXIBLYMODIFYTHEDEVICEIDANDADDRESSOFTHESENSOR.

NOTETHATTHEREISONLYONESENSORONTHE485BUSWHENUSINGAUTOMATICDATAACQUISITIONBYSOFTWARE.

## SENSORCONNECTEDTOCOMPUTER

TherearsensoriscorrectlyconnectedtothecomputerviaUsBto485andprovidespower.

PROPERTIES-DEVICEMANAGER-PORT")

<!-- image -->

Openthedatapackage,select"DebuggingSoftware"-"485ParameterConfigurationSoftwarefind theconfiguration softwareandopenit.

## UseofSensorMonitoringSoftware

<!-- image -->

## COMMUNICATIONBASICPARAMETERS

| Coding                  | 8 bit binary                                                                                                                      |
|-------------------------|-----------------------------------------------------------------------------------------------------------------------------------|
| Databits                | 8bit                                                                                                                              |
| Parity bit              | None                                                                                                                              |
| Stop bit                | 1bit                                                                                                                              |
| Error checking checking | CRCredundant cyclic code                                                                                                          |
| Baudrate                | 1200bit/s,2400bit/s,4800bit/s,9600bit/s,14400bit/s,19200bit/s, Factorydefault:9600bit/s (Youcanmodifyit yourself throughsoffware) |

STORE

## DATAFRAMEFORMATDEFINITION

UsingtheModbus-RTUcommunicationprotocol,theformatisasfollows:

Initial structure≥4bytesoftime

Address code=1byte

Function code=1byte

Data area=Nbytes

## Errorcheck=16-bit CRCcode ZElectronic

Time to end structure≥4 bytes

STORE

Address code:the addressof the transmitter,which is unique in the

communication network (factory defaultx01).

Functioncode:thefunctioninstructionofthecommandsentbythehost,

thistransmitteronly\_usesthefunctioncode

0x03(read register data).

Dataarea:Thedataareaisthespecificcommunicationdata,payattention

tothehighbyteofthe1bitsdatafirst!

## CRC code: two-byte check code.ZElectronic

Hostqueryframestructure:

| L1byte   | 1byte   | 2byte   | 2byte   | 1byte   | 1byte   |
|----------|---------|---------|---------|---------|---------|

Slaveacknowledgmentframestructure

| addresscodeFunction codenumberofvalidbytesdata area second data areaNthdata area checkcode   |       |       |             |       |       |
|----------------------------------------------------------------------------------------------|-------|-------|-------------|-------|-------|
| 1byte                                                                                        | 1byte | 1byte | 2 byte2byte | 2byte | 2byte |

## REGISTERADDRESS

|       |   RegisteraddressPLCorconfiguration address | Content                                                                         | Operate      | Supportfunction   |
|-------|---------------------------------------------|---------------------------------------------------------------------------------|--------------|-------------------|
| 0000H |                                       40001 | Humidity(1otimestheactualvalue                                                  | readonly     | 03                |
| 0001H |                                       40002 | Temperature(10timestheactualvalue)                                              | readonly     | 03                |
| 0100H |                                       40257 | Address                                                                         | readandwrite | 03-06             |
| 0101H |                                       40258 | Baudrate（1for1200.2for2400.3forreadandwrite 4800,4for9600.5for14400.6for19200) |              | 03-06             |
| 0102H |                                       40259 | Humidity address                                                                | readandwrite | 03-06             |
| 0104H |                                       40260 | Temperaturecorrectionvalue                                                      | readandwrite | 03.06             |
| 0105H |                                       40261 | Humiditycorrectionvalue                                                         | readandwrite | 0306              |

STOBE

## COMMUNICATIONPROTOCOLSAMPLESANDEXPLANATIONS

ReadthetemperatureandhumidityvalueofdeviceaddressOx01Ox0lic

Example:ReadthetemperatureandhumidityvalueofdeviceaddressOx01

Query frame (hexadecimal):

STORE

| AddresscodeFunction codeInitial address Datalength Checkcode low Checkcode high   |      |          |          |     |      |
|-----------------------------------------------------------------------------------|------|----------|----------|-----|------|
| 0x01                                                                              | 0x03 | 0x000x00 | 0x000x02 | OxC | OxOB |

Responseframe(hexadecimal):(Forexample,the temperature is-9.7Cand thehumidityis48.6%RH)

| 0x01   | 0x03   | 0x04   | 0x010xE6OxFF0x9F   | 0×1B   | OXAO   |
|--------|--------|--------|--------------------|--------|--------|

Temperaturecalculation:

WhenthetemperatureislowerthanoCthetemperaturedataisuploaded intheformof complementcode.

Temperature:FF9FH(hex=-97=&gt;temperature=-9.7C

Humidity calculation:

Humidity:1E6H(Hex)=486=&gt;Humidity=48.6%RH

## CHANGETHEDEVICEWITHADDRESSO1TO02

Requestframe(hexadecimal):

Responseframe(hexadecimal:

| AddresscodeFunctioncodeRegisteraddressContentsof temperaturecalibration CheckcodelowCheckcodehigh   | AddresscodeFunctioncodeRegisteraddressContentsof temperaturecalibration CheckcodelowCheckcodehigh   | AddresscodeFunctioncodeRegisteraddressContentsof temperaturecalibration CheckcodelowCheckcodehigh   | AddresscodeFunctioncodeRegisteraddressContentsof temperaturecalibration CheckcodelowCheckcodehigh   | AddresscodeFunctioncodeRegisteraddressContentsof temperaturecalibration CheckcodelowCheckcodehigh   |
|-----------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------|
| 0x01                                                                                                | 0x060x010x00                                                                                        | 0x000x02                                                                                            | Ox**                                                                                                | Ox**                                                                                                |

| 0x01   | 0x060x010x00   | 0x000x02   | Ox**   | Ox**   |
|--------|----------------|------------|--------|--------|

## SETTHEBAUDRATEOFDEVICEADDRESSOXO1TO4800

Changethebaudrateofdevice01to480(01means1200,01means2400,02means4800,03means9600,

04means14400,05means19200

## Requestframe(hexadecimal:

<!-- image -->

Responseframe(hexadecimal):

| AddresscodeFunctioncodeRegisteraddressBaudratevalue contentLowbitofverificationcodeHighbitofverificationcode   | AddresscodeFunctioncodeRegisteraddressBaudratevalue contentLowbitofverificationcodeHighbitofverificationcode   | AddresscodeFunctioncodeRegisteraddressBaudratevalue contentLowbitofverificationcodeHighbitofverificationcode   | AddresscodeFunctioncodeRegisteraddressBaudratevalue contentLowbitofverificationcodeHighbitofverificationcode   |
|----------------------------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------|
| 0x01                                                                                                           | 0x060x010x010x000x01                                                                                           | Ox**                                                                                                           | Ox**                                                                                                           |

<!-- image -->

## Common problems and solutions

No output oroutput error possiblereason:

- .ThecomputerhasaCoMport,and theselectedportisincorrect.
- ②,thebaud rate is wrong.
- .The485busisdisconnected,ortheAandBlinesarereversed.
- .lfthenumberofdevicesistoomuchorthewiringistoolong,powersupplyshouldbe providednearby,add485booster,andincrease120terminalresistanceatthesametime.
- .TheUSBto485driverisnotinstalledordamaged.
- ,equipment damage.

Please open the link to download the specific communication protocol and configuration software.

http://www.liyuanz.com/filedownload/107545

<!-- image -->