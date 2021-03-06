/**
 * \author Kevin Browder
 * \copyright GNU Public License.
 **/
#include "PCF85xxRTC.h"
#include <EEPROM.h>
uint64_t makeTimeMilli(tmElementsWithMillis tm) {
	tmElements_t * tmSimple;
	tmSimple = (tmElements_t*) &tm;
	return tm.Milliseconds + makeTimeMilli(*tmSimple);
}
uint64_t makeTimeMilli(tmElements_t tm) {
	return ((uint64_t) makeTime(tm) * 1000);
}

const uint8_t PCF85xx::READ_ADDR = 0xA2 >> 1;
const uint8_t PCF85xx::WRITE_ADDR = 0xA3 >> 1;
PCF85xx PCF85xx::defaultRTC;

EEPROMTimeSaver PCF85xx::defaultTimeSaver = EEPROMTimeSaver();

void PCF85xx::initControlReg() {
	memset(&this->controlReg, 0, sizeof(this->controlReg));
}

PCF85xx::PCF85xx() {
	this->init(TwoWire(), &this->defaultTimeSaver);
}
PCF85xx::~PCF85xx() {
}

PCF85xx::PCF85xx(TwoWire wire, AbstractTimeSaver * timeSaver) {
	this->init(wire, timeSaver);
}
void PCF85xx::init(TwoWire wire, AbstractTimeSaver * timeSaver) {
	this->timeSaver = timeSaver;
	this->wire = wire;
	this->wire.begin();
}

PCF85xx * PCF85xx::getDefaultRTC() {
	return &(PCF85xx::defaultRTC);
}
time_t PCF85xx::getDefaultTime() {
	return PCF85xx::getDefaultRTC()->get();
}
void PCF85xx::setDefaultTime(time_t t) {
	PCF85xx::getDefaultRTC()->set(t);
}
uint8_t PCF85xx::to_uint8(void * s) {
	uint8_t * i = (uint8_t*) s;
	return *i;
}
void PCF85xx::setup() {
	this->writeByte(this->STATUS_CONTROL_REG,
			this->to_uint8(&this->controlReg));
}
void PCF85xx::reset() {
	this->setup();
	this->clearSave();
	tmElements_t tm;
	tm.Year = CalendarYrToTm(2011);
	tm.Month = 12;
	tm.Day = 31;
	tm.Wday = 6;
	tm.Hour = 23;
	tm.Minute = 59;
	tm.Second = 59;

	this->write(tm);

}

time_t PCF85xx::get() {
	tmElements_t tm;
	this->read(tm);
	return makeTime(tm);
}
void PCF85xx::read(tmElementsWithMillis &tm) {
	this->wire.beginTransmission(this->READ_ADDR);
	this->wire.write(this->HUNDRETH_SEC_REG);
	this->wire.endTransmission();
	TIME time;

	this->wire.requestFrom(this->READ_ADDR, sizeof(time));
	this->wire.readBytes((char*) &time, sizeof(time));

	tm.Second = time.seconds.ten * 10L + time.seconds.unit;
	tm.Minute = time.minutes.ten * 10L + time.minutes.unit;
	//for hour the two msb are The "12 Hour flag" (bit 7)
	//  and the subordinant "pm flag" (bit 6)
	tm.Hour = time.hours.hour_t * 10L + time.hours.hour_u;
	tm.Day = time.day_year.day_t;
	tm.Day *= (uint8_t) 10;
	tm.Day += (uint8_t) time.day_year.day_u;

	uint16_t eeprom_year = year(this->load() / 1000);
	uint16_t year = eeprom_year + (uint16_t) (time.day_year.year_off);
	tm.Year = CalendarYrToTm(year);

	tm.Wday = time.wday_month.dow;
	tm.Month = time.wday_month.month_t * 10L + time.wday_month.month_u;

	tm.Milliseconds = 10
			* ((uint16_t) (time.hundredths.ten * 10 + time.hundredths.unit));
	//update eeprom if year_off > 0 so next time year_off=0
	// ensuring we're never overflow 3 bits.
	if (time.day_year.year_off > 0) {
		this->save(makeTimeMilli(tm));
		time.day_year.year_off = 0;
		this->writeByte(this->YEAR_REG, this->to_uint8(&time.day_year));
	}
}
void PCF85xx::read(tmElements_t &tm) {
	tmElementsWithMillis tmMillis;
	this->read(tmMillis);
	memcpy(&tm, &tmMillis, sizeof(tm));
}

void PCF85xx::set(time_t t) {
	tmElements_t tm;
	breakTime(t, tm);
	this->write(tm);
}
void PCF85xx::write(tmElementsWithMillis &tm) {
	uint64_t eeprom_time = this->load();

	uint16_t year_off = (int) tmYearToCalendar(tm.Year)
			- (int) year(eeprom_time / 1000);
	if ((!eeprom_time) == 0 || year_off >= 3 || year_off < 0) { //unset eeprom or bad offsets
		eeprom_time = makeTimeMilli(tm);
		this->save(eeprom_time);
		year_off = 0;
	}
	TIME time;

	time.hours.is24hr = time.hours.am = 0;
	time.hours.hour_t = tm.Hour / 10;
	time.hours.hour_u = tm.Hour % 10;

	time.day_year.day_t = tm.Day / 10;
	time.day_year.day_u = tm.Day % 10;

	time.day_year.year_off = year_off;

	time.wday_month.month_t = tm.Month / 10;
	time.wday_month.month_u = tm.Month % 10;
	time.wday_month.dow = tm.Wday;

	time.minutes.ten = tm.Minute / 10;
	time.minutes.unit = tm.Minute % 10;

	time.seconds.ten = tm.Second / 10;
	time.seconds.unit = tm.Second % 10;

	time.hundredths.ten = (tm.Milliseconds / 10) / 10;
	time.hundredths.unit = (tm.Milliseconds / 10) % 10;

	this->wire.beginTransmission(this->WRITE_ADDR);
	this->wire.write(this->HUNDRETH_SEC_REG); //Starting address
	uint8_t * data = (uint8_t*) &time;
	for (uint8_t i = 0; i < sizeof(time); i++) {
		this->wire.write(data[i]);
	}
	this->wire.endTransmission();
}
void PCF85xx::write(tmElements_t &tm) {
	tmElementsWithMillis tmMillis;
	memset(&tmMillis, 0, sizeof(tmMillis));
	memcpy(&tmMillis, &tm, sizeof(tm));
	this->write(tmMillis);
}
void PCF85xx::writeByte(uint8_t word, uint8_t value) {
	this->wire.beginTransmission(this->WRITE_ADDR);
	this->wire.write(word);
	this->wire.write(value);
	this->wire.endTransmission();
}
uint8_t PCF85xx::readByte(uint8_t word) {
	char result = 0;
	this->wire.beginTransmission(this->READ_ADDR);
	this->wire.write(word);
	this->wire.endTransmission();
	this->wire.requestFrom(this->READ_ADDR, (uint8_t) 1);
	if (1 <= this->wire.available()) {
		this->wire.readBytes(&result, (long unsigned int) 1);
	}
	return (uint8_t) result;
}

void PCF85xx::save(timems_t time) {
	this->timeSaver->save(time);
}
timems_t PCF85xx::load() {
	return this->timeSaver->load();
}

void PCF85xx::clearSave() {
	this->save(((timems_t) -1));
}
