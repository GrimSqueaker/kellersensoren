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

#include <curl/curl.h>

#include "argh.h"
 
#define MAXTIMINGS	85
#define DHTPIN		7 // 7 = vorne, 0 = mitte, 2 = hinten

namespace config
{
    bool verbose = false;
    bool local = false;
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

namespace
{
	static size_t read_callback(void *ptr, size_t size, size_t nmemb, void *mydata)
	{
        strncpy(static_cast<char*>(ptr), static_cast<char*>(mydata), size * nmemb);
        return size * nmemb;
	}
} // namespace

void sendToOpenHAB(const std::string &url, const std::string &send_data)
{
	if (config::local) {
		return;
	}
	CURL *curl = curl_easy_init();
	if (curl) {
		curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
		curl_easy_setopt(curl, CURLOPT_VERBOSE, config::verbose ? 1L : 0L);
		curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
		curl_easy_setopt(curl, CURLOPT_PUT, 1L);
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_READDATA, send_data.c_str());
		curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)send_data.size());

		CURLcode res = curl_easy_perform(curl);
		if (res != CURLE_OK)
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;

		curl_easy_cleanup(curl);
	}
}


struct Raum {
    int gpio_pin;
    std::string room_name;
    std::string humidity_url;
    std::string temperature_url;
    std::string dewpoint_url;
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
            "http://ratzpi:8080/rest/items/Luftfeuchtigkeit_Keller_Vorne/state",
            "http://ratzpi:8080/rest/items/Temperatur_Keller_Vorne/state",
            "http://ratzpi:8080/rest/items/Taupunkt_Keller_Vorne/state",
        },
        {
            0,
            "Keller mitte",
            "http://ratzpi:8080/rest/items/Luftfeuchtigkeit_Keller_Mitte/state",
            "http://ratzpi:8080/rest/items/Temperatur_Keller_Mitte/state",
            "http://ratzpi:8080/rest/items/Taupunkt_Keller_Mitte/state",
        },
        {
            2,
            "Keller hinten",
            "http://ratzpi:8080/rest/items/Luftfeuchtigkeit_Keller_Hinten/state",
            "http://ratzpi:8080/rest/items/Temperatur_Keller_Hinten/state",
            "http://ratzpi:8080/rest/items/Taupunkt_Keller_Hinten/state",
        },
    }
};


Sensordata read_dht11_dat(int gpio_pin)
{
    uint8_t laststate = HIGH;
    uint8_t counter = 0;
    uint8_t j = 0, i;
    int dht11_dat[5] = { 0, 0, 0, 0, 0 };

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
            dht11_dat[j / 8] <<= 1;
            if ( counter > 16 ) {
                dht11_dat[j / 8] |= 1;
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

int main(int, char* argv[])
{
    // setup: CLI args
    argh::parser cmdl(argv);
    if (cmdl[{ "-v", "--verbose" }]) {
        config::verbose = true;
    }
    if (cmdl[{ "-l", "--local" }]) {
        config::local = true;
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
  
    // setup: CURL
    if (!config::local) curl_global_init(CURL_GLOBAL_ALL);

    while (1) {
        for (auto room: KELLER) {
            if (config::verbose)
                std::cout << "\nRaum " << room.room_name << '\n';

            Sensordata data = read_dht11_dat(room.gpio_pin);
            if (data.good) {
                float humidity = convertIntFracToDouble(data.humidity_integer, data.humidity_fraction);
                float temperature = convertIntFracToDouble(data.temperature_integer, data.temperature_fraction);
                float dewpoint = computeDewPoint(temperature, humidity);
                std::time_t update_time = std::time(nullptr);
                std::tm *localtime = std::localtime(&update_time);
//                std::string update_time_string = 
//                    std::to_string(1900+localtime->tm_year) + "-"
//                    + std::to_string(1+localtime->tm_mon) + "-"
//                    + std::to_string(localtime->tm_mday) + "-"
//                    + std::to_string(1+localtime->tm_hour) + "-"
//                    + std::to_string(1+localtime->tm_min) + "-"
//                    + std::to_string(1+localtime->tm_sec);

                if (config::verbose)
//                    std::cout << "Time = " << update_time_string << "   "
                    std::cout << "Temperature = " << temperature << "C   "
                              << "Humidity = " << humidity << "%   "
                              << "Dew point = " << dewpoint << "C   "
                              << "\n";

                std::string sendString;
                sendString = std::to_string(data.humidity_integer) + "." + std::to_string(data.humidity_fraction);
                sendToOpenHAB(room.humidity_url, sendString);
                sendString = std::to_string(data.temperature_integer) + "." + std::to_string(data.temperature_fraction);
                sendToOpenHAB(room.temperature_url, sendString);
                std::ostringstream conv;
                conv << std::fixed << std::setprecision(1) << dewpoint;
                sendString = conv.str();
                sendToOpenHAB(room.dewpoint_url, sendString);
            }
        }
        delay(10000); 
    }

    if (!config::local) curl_global_cleanup();

    return EXIT_SUCCESS;
}
