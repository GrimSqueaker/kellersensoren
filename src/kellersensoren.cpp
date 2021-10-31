#include <wiringPi.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>

#include <array>
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>

#include "argh.h"
 
#define MAXTIMINGS	85
#define DHTPIN		7 // 7 = vorne, 0 = mitte, 2 = hinten

namespace config
{
    bool verbose = false;
}


float convertIntFracToDouble(int integer, int fraction) {
    float result = integer;
    if (fraction < 10)
        result += fraction / 10.0;
    else
        result += fraction / 100.0;

    return result;
}

float computeDewPoint(float temperature, float relHumidity) {
    // using the Magnus formula
    // constants a,b,c taken from: https://en.wikipedia.org/wiki/Dew_point#Calculating_the_dew_point
    float a = 6.112, b = 17.67, c = 243.5;

    float gamma = std::log(relHumidity/100.0) + (b*temperature)/(c + temperature);
    float P_a = a*std::exp(gamma); // ... actual vapor pressure
    return (c*std::log(P_a/a)) / (b - std::log(P_a/a));
}

void saveToFile(const std::string &file_name, const std::string &data)
{
    std::ofstream os(file_name);
    if (os) { 
      os << data;
    }
}


struct Raum {
    int gpio_pin;
    std::string room_name;
    std::string update_time_file;
    std::string humidity_file;
    std::string temperature_file;
    std::string dewpoint_file;
    std::vector<float> temperature_history;
    std::vector<float> humidity_history;
};

struct Sensordata {
    bool good;
    int humidity_integer;
    int humidity_fraction;
    int temperature_integer;
    int temperature_fraction;
};

std::vector<Raum> KELLER = {
    {
        {
            7,
            "Keller vorne",
            "/var/www/html/kellersensoren/keller/vorne/updatezeit.txt",
            "/var/www/html/kellersensoren/keller/vorne/luftfeuchtigkeit.txt",
            "/var/www/html/kellersensoren/keller/vorne/temperatur.txt",
            "/var/www/html/kellersensoren/keller/vorne/taupunkt.txt",
	    {}, {},
        },
        {
            0,
            "Keller mitte",
            "/var/www/html/kellersensoren/keller/mitte/updatezeit.txt",
            "/var/www/html/kellersensoren/keller/mitte/luftfeuchtigkeit.txt",
            "/var/www/html/kellersensoren/keller/mitte/temperatur.txt",
            "/var/www/html/kellersensoren/keller/mitte/taupunkt.txt",
	    {}, {},
        },
        {
            2,
            "Keller hinten",
            "/var/www/html/kellersensoren/keller/hinten/updatezeit.txt",
            "/var/www/html/kellersensoren/keller/hinten/luftfeuchtigkeit.txt",
            "/var/www/html/kellersensoren/keller/hinten/temperatur.txt",
            "/var/www/html/kellersensoren/keller/hinten/taupunkt.txt",
	    {}, {},
        },
    }
};


Sensordata read_dht11_dat(int gpio_pin)
{
    uint8_t laststate = HIGH;
    uint8_t counter = 0;
    uint8_t j = 0, i;
    std::array<int, 6> dht11_dat = { 0, 0, 0, 0, 0, 0 };

    pinMode( gpio_pin, OUTPUT );
    digitalWrite( gpio_pin, HIGH );
    delayMicroseconds( 40 );
    digitalWrite( gpio_pin, LOW );
    delay( 18 );
    pinMode( gpio_pin, INPUT );

    for ( i = 0; i < MAXTIMINGS; i++ ) {
        counter = 0;
        while ( digitalRead( gpio_pin ) == laststate ) {
            counter++;
            delayMicroseconds( 1 );
            if ( counter == 255 ) {
                break;
            }
        }
        laststate = digitalRead( gpio_pin );

        if ( counter == 255 )
            break;

        if ( (i >= 4) && (i % 2 == 0) ) {
            dht11_dat.at(j / 8) <<= 1;
            if ( counter > 16 ) {
                dht11_dat.at(j / 8) |= 1;
            }
            j++;
        }
    }

    bool good = false;
    if ( (j >= 40) && (dht11_dat[4] == ( (dht11_dat[0] + dht11_dat[1] + dht11_dat[2] + dht11_dat[3]) & 0xFF) ) ) {
        good = true;
    }
    else  {
        if (config::verbose)
            std::cout << "     Checksum error, raw data = "
                      << dht11_dat[0] << ", " << dht11_dat[1] << ", "
                      << dht11_dat[2] << ", " << dht11_dat[3] << ", " << dht11_dat[4] << '\n';
    }

    Sensordata data = {good, dht11_dat[0], dht11_dat[1], dht11_dat[2], dht11_dat[3]};
    return data;
}

float updateHistoryAndReturnMedian(std::vector<float>& history, float value)
{
    if (history.size() == 0) {
	history.resize(5);
        history[0] = value;
        history[1] = value;
        history[2] = value;
        history[3] = value;
        history[4] = value;
    }

    history[4] = history[3];
    history[3] = history[2];
    history[2] = history[1];
    history[1] = history[0];
    history[0] = value;

    auto shist = history;
    std::sort(shist.begin(), shist.end());

    return shist[2];
}

int main(int, char* argv[])
{
    // setup: CLI args
    argh::parser cmdl(argv);
    if (cmdl[{ "-v", "--verbose" }]) {
        config::verbose = true;
    }
    if (cmdl[{ "--help" }]) {
        std::cout << "kellersensoren" << '\n';
        std::cout << "  --help          This help" << '\n';
        std::cout << "  -v, --verbose   Verbose output" << '\n';
        return EXIT_SUCCESS;
    }

    // setup: wiringPi
    if ( wiringPiSetup() == -1 ) {
        std::cout << "Error in wiringPiSetup\n";
        return EXIT_FAILURE;
    }

    while (1) {
        for (auto room: KELLER) {
            if (config::verbose)
                std::cout << "\nRaum " << room.room_name << '\n';

            Sensordata data = read_dht11_dat(room.gpio_pin);
            if (data.good) {
                float humidity = convertIntFracToDouble(data.humidity_integer, data.humidity_fraction);
                float temperature = convertIntFracToDouble(data.temperature_integer, data.temperature_fraction);

                std::time_t update_time = std::time(nullptr);
                std::tm *localtime = std::localtime(&update_time);
                std::string update_time_string = 
                    std::to_string(1900+localtime->tm_year) + "-"
                    + std::to_string(1+localtime->tm_mon) + "-"
                    + std::to_string(localtime->tm_mday) + "T"
                    + std::to_string(1+localtime->tm_hour) + ":"
                    + std::to_string(1+localtime->tm_min) + ":"
                    + std::to_string(1+localtime->tm_sec);
                if (config::verbose)
                    std::cout << "Time = " << update_time_string
			      << "\n"
                              << "Raw Temperature = " << temperature << "C   "
                              << "Raw Humidity = " << humidity << "%   "
                              << "\n";

                humidity = updateHistoryAndReturnMedian(room.humidity_history, humidity);
                temperature = updateHistoryAndReturnMedian(room.temperature_history, temperature);

                float dewpoint = computeDewPoint(temperature, humidity);

                if (config::verbose)
                    std::cout << "Temperature = " << temperature << "C   "
                              << "Humidity = " << humidity << "%   "
                              << "Dew point = " << dewpoint << "C   "
                              << "\n";

                std::string sendString;
                saveToFile(room.update_time_file, update_time_string);
                sendString = std::to_string(data.humidity_integer) + "." + std::to_string(data.humidity_fraction);
                saveToFile(room.humidity_file, sendString);
                sendString = std::to_string(data.temperature_integer) + "." + std::to_string(data.temperature_fraction);
                saveToFile(room.temperature_file, sendString);
                std::ostringstream conv;
                conv << std::fixed << std::setprecision(1) << dewpoint;
                sendString = conv.str();
                saveToFile(room.dewpoint_file, sendString);
            }
        }
        delay(10000); 
    }

    return EXIT_SUCCESS;
}
