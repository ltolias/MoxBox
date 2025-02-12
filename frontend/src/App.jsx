import React from 'react'
import { useState, useCallback, useEffect} from 'react'
import _ from 'underscore';
import localforage from 'localforage';

import Utilities from './utilities'

import Dashboard from './Dashboard'
import Settings from './Settings'
import Peers from './Peers'


import History from './History'
import {Tabs, Tab} from './Tabs'

const App = () => {

  const server_ip_stored = useCallback(
    () => localStorage.getItem('server_ip') ? 
      localStorage.getItem('server_ip') : 'moxbox.local', 
  []) //makes this only load from localstorage once

  const [state, setState] = useState({
    server_ip:  server_ip_stored(),
  });
  useEffect(() => {
    (async () => {
      localforage.setDriver(localforage.INDEXEDDB)
    })();
    return () => {
    }
  }, []);
    

  const util = Utilities(state, setState, state.server_ip);


  const set_server_ip = (ip) => {
    localStorage.setItem('server_ip', ip)
    setState((prevState) => ({ ...prevState, server_ip: ip }))
  }

  return (
      <Tabs title="MoxBox">
        <Tab label="Dashboard" >
          <Dashboard server_ip={state.server_ip} />
        </Tab>
        <Tab label="History">
            <History server_ip={state.server_ip}/>
        </Tab>
        <Tab label="Peers" >
            <Peers server_ip={state.server_ip} set_server_ip={set_server_ip}/>
        </Tab>
        <Tab label="Settings">
            <Settings server_ip={state.server_ip}/>
        </Tab>
      </Tabs>
  )

}

export default App
