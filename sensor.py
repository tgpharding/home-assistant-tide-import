# custom_components/met_ocean_tide/sensor.py
"""Platform for MetOcean Tide sensor."""
from datetime import timedelta
import logging
import requests
import voluptuous as vol

from homeassistant.components.sensor import PLATFORM_SCHEMA
from homeassistant.const import CONF_NAME
from homeassistant.helpers.entity import Entity

_LOGGER = logging.getLogger(__name__)

SCAN_INTERVAL = timedelta(hours=1)

PLATFORM_SCHEMA = PLATFORM_SCHEMA.extend({
    vol.Required(CONF_NAME): vol.Coerce(str),
})

def setup_platform(hass, config, add_entities, discovery_info=None):
    """Set up the MetOcean Tide sensor."""
    name = config.get(CONF_NAME)

    add_entities([MetOceanTideSensor(name)], True)

class MetOceanTideSensor(Entity):
    """Representation of a MetOcean Tide sensor."""

    def __init__(self, name):
        """Initialize the sensor."""
        self._name = name
        self._state = None

    @property
    def name(self):
        """Return the name of the sensor."""
        return self._name

    @property
    def state(self):
        """Return the state of the sensor."""
        return self._state

    def update(self):
        """Fetch tide information."""
        # Add logic to fetch tide information from MetOcean API
        # Replace the URL and API key with your actual values
        url = "https://forecast-api.metoceanapi.com/tide-info?location=Raglan&api_key=PgUVxmAGuE52AntxpDwgPQ"
        
        try:
            response = requests.get(url)
            data = response.json()
            
            # Extract relevant information from the response
            tide_height = data.get("tide_height")
            
            # Update the state of the sensor
            self._state = tide_height
        except Exception as e:
            _LOGGER.error(f"Error fetching tide information: {e}")
            self._state = None
