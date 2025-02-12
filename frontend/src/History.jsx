import React from 'react';
import { useState, useEffect, useCallback} from 'react'
import _ from 'underscore';
import localforage from 'localforage';

import ChartView from './ChartView';
import Utilities from './utilities'


const fields = ['time', 'value']

const History = (props) => {
    
    const [state, setState] = useState({
        graph_data: {},
        latest_log_indexes: [],
        timings_selected: 0,
        timings: []
    });
    const util = Utilities(state, setState, props.server_ip)

    const chart_ref = React.useRef(null);
    var history_visible_ref = React.useRef(false);

    useEffect(() => {
        (async () => {
            history_visible_ref.current = true;
            try {
                const graph_data_stored = await localforage.getItem('graph_data')
                if (graph_data_stored) {
                    setState((prevState) => ({...prevState, graph_data: graph_data_stored}))
                }
            } catch (error) {
                console.error('Error loading existing graph data:', error);
            }
            try {
                await util.device_to_state('timings')
                await util.device_to_state('latest_log_indexes')
            } catch (error) {
                console.error('Error fetching data:', error);
            }
        })();
        return () => {
            history_visible_ref.current = false;
        }
    }, []);

    useEffect(() => {
        (async () => {
            try {
                await load_graphs()
            } catch (error) {
                console.error('Error fetching data:', error);
            }
        })();
    }, [state.latest_log_indexes, state.timings_selected]);

   
    const timescale_change = (index) => async (event) => {
        if (event.target.checked) {
            await setState((state) => ({...state, timings_selected: index}))
        }
    }

    const cached_fetch_convert = async (file_path) => {
        try {
            const cached_file = await localforage.getItem(file_path)
            if (cached_file != null) {
                return cached_file
            } else {
                const response = await util.fetch(file_path)
                if (response.status == 404) { //log file never recorded or has since been deleted - don't keep looking for it
                    return [] //todo: set cache here so we don't look again in the future, but need to check race condition with 
                    //  devices file creation/writing/buffering first
                }
                const text = await response.text()
                const lines = _.filter(text.split('\n'))

                const file_contents = _.map(lines, line => {
                    const line_split = line.split(',')
                    return _.object(
                        _.map(fields, (field, i) => [field, line_split[i]] )
                    )
                })
                if (response.headers.has("X-Moxbox-Cache") && response.headers.get("X-Moxbox-Cache") == "yes") {
                    localforage.setItem(file_path, file_contents)
                }
                return file_contents
            }
        } catch (err) {
            console.error('error fetching graph data: ', err)
            if (history_visible_ref.current) {
                // try again in a few seconds if were still on this tab
                await new Promise(r => setTimeout(r, 2000));
                return await cached_fetch_convert(file_path)
            }
        }
    }

    const load_graphs = async () => {
        let datas = {}
        for (let logger_detail of state.latest_log_indexes) {
            const {path, mins_per_file, latest_index } = logger_detail

            if (mins_per_file != state.timings[state.timings_selected].mins_per_file){
                continue //todo: this loop just to pull out two things isn't great
            }
            const data_name = path.split('/')[2]

            const discard_after_hrs = state.timings[state.timings_selected].discard_after_hrs
            const start_index = Math.max(0, latest_index - Math.floor(discard_after_hrs * 60 / mins_per_file) ) 
            //we're purposefully getting 13 indexes per hour, as the latest one will be incomplete
            const idxes_needed = _.range(start_index,latest_index + 1)
            // console.log(idxes_needed)
            const graphs_needed = _.compact(_.map(idxes_needed, idx => idx > -1 ? `sdcard${path}/${idx}` : undefined))
            const data_files = []
            for (let file_path of graphs_needed) {
            const data_file = await cached_fetch_convert(file_path)
            if (data_file) {
                data_files.push(data_file)
            }
            }
            const data = _.flatten(data_files)
            const time_shifted = _.map(data, el => ({...el, time: Math.floor(-1/10/60*(data[data.length-1].time - el.time))/100}))
            datas[data_name] = time_shifted
        }
        if (_.size(datas) > 0)
        {
            localforage.setItem('graph_data', datas)
            setState((prevState) => ({...prevState, graph_data: datas}))
        }
    }


    const reset_zoom = () => {
        if (chart_ref && chart_ref.current) {
            chart_ref.current.resetZoom();
        }
    };

    let graph_view = <div className='pane top-pane graph-pane'/>
    if (_.size(state.graph_data) > 0) {
        graph_view = <div className='pane top-pane graph-pane'>
            <ChartView data={state.graph_data} chartRef={chart_ref} style={{height: '100%'}}/>
        </div>
    } 
    return <div style={{overflow: 'hidden'}}>
        {graph_view}
        <div style={{height: '10px'}}/>
        <div className='pane flex-row input-container' style={{ height: '40px'}}>
            <button onClick={reset_zoom} style={{width: '50%'}}>Reset Zoom</button>
            <div className='input-divider'/>
            <button onClick={() => load_graphs()} style={{width: '50%'}}>Reload Graph</button>
        </div>
        <div style={{height: '10px'}}/>
        <div className='pane flex-row input-container' style={{ height: '50px'}}>
            <span key={`graph-timing-options-title`} className='input-label light' > Timescales (seconds): </span>
            <div className='input-divider'/>
            <div className='flex-row dark' style={{width: '100%'}}>
            {_.map(state.timings, (timing, i) => {
                const name = Math.floor(timing.ms_per_row/1000)
                return [
                    <label key={`graph-timing-option-${name}`} className='checkbox-with-label' style={{width: '100%'}}>
                        <div className='checkbox-box' style={{margin: 'auto'}}>
                            <input  type='checkbox'  onChange={timescale_change(i)} checked={state.timings_selected == i}/> 
                        </div>
                        <label className='input-label' style={{width: '30px', margin: 'auto', marginTop: '4px'}}> {name} </label>
                    </label>
                    , i < _.size(state.timings) - 1 ? <div className='input-divider' key={`timing-option-divider-${i}`}/> : null
                ]
            })}
            </div>
        </div>
    </div>
};

export default History;